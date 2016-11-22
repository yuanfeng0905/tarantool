/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
extern "C" {
#include "hash.h"
#include "sql.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "sqlite3.h"
#include "sqliteInt.h"
#include "btreeInt.h"
}

#undef likely
#undef unlikely

#include "say.h"
#include "box/index.h"
#include "box/schema.h"
#include "box/txn.h"
#include "box/tuple.h"
#include "box/session.h"
#include "box/key_def.h"
#include "trigger.h"
#include "small/rlist.h"
#include "sql_mvalue.h"
#include "trivia/util.h"

#include "sql_tarantool_cursor.h"
#include "lua/space_iterator.h"

#include <memory> /* unique_ptr */

template<typename T, typename Del>
std::unique_ptr<T, Del> unique_ptr_with_deletor(T *instance, Del &&deletor)
{
	return std::unique_ptr<T, Del>(instance,
				       std::forward<Del>(deletor));
}

const int MAX_TYPE_LEN = 32;
const int ESTIMATED_ROW_SIZE = 100;

/**
 * Structure for linking BtCursor (sqlite) with
 * his tarantool backend - TarantoolCursor
 */
struct TrntlCursor {
	BtCursor *brother; /* BtCursor for TarantoolCursor */
	TarantoolCursor cursor;
	char *key; /* Key for creating box_index_iterator */
};

/**
 * Structure that contains objects needed by API functions.
 * API see below.
 */
struct sql_tarantool_state {
	trigger space_on_replace;
	trigger index_on_replace;
	trigger trigger_on_replace;
	TrntlCursor **cursors; /* All cursors, opened now */
	int cnt_cursors; /* Size of cursors array */
	SIndex **indices; /* All tarantool indices */
	int cnt_indices; /* Size of indices */
};

/**
 * Converts struct Table object into msgpuck representation
 */
static char *
make_msgpack_from_table(sqlite3 *db, const Table *table, int &size, const char *crt_stmt);

/**
 * Converts struct SIndex object into msgpuck representation
 */
static char *
make_msgpack_from_index(const SIndex *index, int &size);

/**
 * Get maximal ID of all records in space with id cspace_id.
 */
static int
get_max_id_of_space(int cspace_id=BOX_SPACE_ID);

/**
 * Get maximal ID of all records in _index where space id = space_id
 */
static int
get_max_id_of_index(int space_id);

/**
 * Get maximal trigger ID of all records in _trigger where space id = space_id
 */
static int
get_max_id_of_trigger(int space_id);

/**
 * Insert new struct Table object into _space after converting it
 * into msgpuck.
 */
static int
insert_new_table_as_space(sqlite3 *db, Table *table, const char *crt_stmt);

/**
 * Insert new struct Table object into _space as view after converting it
 * into msgpuck.
 */
static int
insert_new_view_as_space(sqlite3 *db, Table *table, const char *crt_stmt);

/**
 * Insert new struct SIndex object into _space after converting it
 * into msgpuck.
 */
static int
insert_new_sindex_as_index(SIndex *index);

/**
 * Insert new struct Trigger object into _trigger after converting it
 * into msgpuck.
 */
static int
insert_trigger(struct sqlite3 *db, Trigger *trigger, char *crt_stmt);

/**
 * This function converts space from msgpuck tuple to
 * sqlite3 Table structure.
 */
static Table *
get_trntl_table_from_tuple(box_tuple_t *tpl,sqlite3 *db,
	Schema *pSchema, bool *is_temp = NULL, bool *is_view = NULL,
	bool is_delete = false);

/**
 * This function converts index from msgpuck tuple to
 * sqlite3 SIndex structure.
 */
static SIndex *
get_trntl_index_from_tuple(box_tuple_t *index_tpl, sqlite3 *db,
	Table *table, bool &ok);

/**
 * Drop index of space space_id with id = index_id. Also removes index
 * from sqlite structures.
 */
static int
drop_index(int space_id, int index_id);

/**
 * Remove all indices of space with id = space_id
 */
static int
drop_all_indices(int space_id);

static field_type
get_tarantool_type_from_sql_aff(int affinity)
{
	switch(affinity) {
	case SQLITE_AFF_REAL:
		return FIELD_TYPE_NUMBER;
	case SQLITE_AFF_NUMERIC:
	case SQLITE_AFF_INTEGER:
		return FIELD_TYPE_INTEGER;
	case SQLITE_AFF_TEXT:
		return FIELD_TYPE_STRING;
	case SQLITE_AFF_BLOB:
		return FIELD_TYPE_SCALAR;
	default:
		return FIELD_TYPE_ANY;
	}
}

/**
 * Create fictive root page number from space_id and index_number.
 */
static uint32_t
make_index_id(uint32_t space_id, uint32_t index_number)
{
	uint32_t res = 0;
	uint32_t first = 1 << 30;
	uint32_t second = (index_number << 28) >> 2;
	uint32_t third = (space_id << 15) >> 6;
	res = first | second | third;
	return res;
}

/**
 * Create fictive root page number from space_id. Index id in that
 * case is 15.
 */
static uint32_t
make_space_id(uint32_t space_id)
{
	return make_index_id(space_id, 15);
}

/**
 * Get space id from root page number.
 */
static uint32_t
get_space_id_from(uint32_t num)
{
	return (num << 6) >> 15;
}

/**
 * Get index id from root page number.
 */
static uint32_t
get_index_id_from(uint32_t num)
{
	return ((num << 2) >> 28);
}

static void
state_add_index(sql_tarantool_state *state, SIndex *index);

static void
state_remove_index(sql_tarantool_state *state, SIndex *index);

/**
 * Applies commited changes to sqlite schema.
 */
static void
on_commit_space(struct trigger *trigger, void *event)
{
	say_debug("%s():", __func__);
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_last_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;
	sqlite3 *db = (sqlite3 *)trigger->data;
	Hash *tblHash = &db->aDb[0].pSchema->tblHash;
	Schema *pSchema = db->aDb[0].pSchema;
	bool is_temp, is_view;
	bool is_delete = false;
	if (old_tuple != NULL) {
		is_delete = (new_tuple == NULL);
		say_debug("%s(): old_tuple != NULL", __func__);
		auto table_deleter = [=](Table *ptr){
			if (!ptr) return;
			sqlite3DbFree(db, ptr->zName);
			for (int i = 0; i < ptr->nCol; ++i) {
				sqlite3DbFree(db,
					      ptr->aCol[i].zName);
				sqlite3DbFree(db,
					      ptr->aCol[i].zType);
			}
			sqlite3DbFree(db, ptr->aCol);
			sqlite3DbFree(db, ptr);
		};
		std::unique_ptr<Table, typeof(table_deleter)> table(
			get_trntl_table_from_tuple(old_tuple, db, pSchema,
						   &is_temp, &is_view, is_delete),
			table_deleter
		);
		if (!table.get()) {
			say_debug("%s(): error while getting table", __func__);
			return;
		}
		if (is_temp) {
			tblHash = &db->aDb[1].pSchema->tblHash;
			pSchema = db->aDb[1].pSchema;
			table->pSchema = pSchema;
		}
		std::unique_ptr<Table, typeof(table_deleter)> schema_table(
			(Table *)sqlite3HashFind(tblHash, table->zName),
			table_deleter
		);
		if (!schema_table.get()) {
			say_debug("%s(): table was not found", __func__);
			return;
		}

		sqlite3HashInsert(tblHash, table->zName, NULL);
	}
	if (new_tuple != NULL) {
		say_debug("%s(): new_tuple != NULL", __func__);
		Table *table = get_trntl_table_from_tuple(new_tuple, db,
							  pSchema,
							  &is_temp,
							  &is_view);
		if (table == NULL) {
			say_debug("%s(): error while getting table", __func__);
			return;
		}
		if (is_temp) {
			tblHash = &db->aDb[1].pSchema->tblHash;
			pSchema = db->aDb[1].pSchema;
			table->pSchema = pSchema;
		}
		sqlite3HashInsert(tblHash, table->zName, table);
	}
}

/**
 * Call every time when _space is modified. This function
 * doesn't do any updates but creating new trigger on commiting
 * this _space updates.
 */
static void
on_replace_space(struct trigger *trigger, void *event) {
	say_debug("%s():", __func__);
	struct txn *txn = (struct txn *) event;
	struct trigger *on_commit = (struct trigger *)
		region_calloc_object_xc(&fiber()->gc, struct trigger);
	trigger_create(on_commit, on_commit_space, trigger->data, NULL);
	txn_on_commit(txn, on_commit);
}

/**
 * Calls every time when _index is updated and this
 * changes are commited. It applies commited changes to
 * sqlite schema.
 */
static void
on_commit_index(struct trigger *trigger, void *event)
{
	say_debug("%s():", __func__);
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_last_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;
	sqlite3 *db = (sqlite3 *)trigger->data;
	Hash *idxHash = &db->aDb[0].pSchema->idxHash;
	sql_tarantool_state *state = db->tarantool.state;
	if (old_tuple != NULL) {
		say_debug("%s(): old_tuple != NULL", __func__);
		bool ok;
		SIndex *index = get_trntl_index_from_tuple(old_tuple, db, NULL, ok);
		if (index == NULL) {
			say_debug("%s(): index is null", __func__);
			return;
		}
		if (sqlite3SchemaToIndex(db, index->pSchema)) {
			idxHash = &db->aDb[1].pSchema->idxHash;
		}
		Table *table = index->pTable;
		SIndex *prev = NULL, *cur;
		ok = false;
		for (cur = table->pIndex; cur != NULL;
		    prev = cur, cur = cur->pNext)
		{
			if (cur->tnum == index->tnum) {
				ok = true;
				if (!prev) {
					table->pIndex = cur->pNext;
					break;
				}
				if (!cur->pNext) {
					prev->pNext = NULL;
					break;
				}
				prev->pNext = cur->pNext;
				break;
			}
		}
		if (!ok) {
			say_debug("%s(): index was not found "
				  "in sql schema", __func__);
			return;
		}
		state_remove_index(state, cur);
		for (int i = 0; i < table->nCol; ++i) {
			sqlite3DbFree(db, index->azColl[i]);
		}
		sqlite3DbFree(db, index);
		sqlite3HashInsert(idxHash, cur->zName, NULL);
		for (int i = 0; i < table->nCol; ++i) {
			sqlite3DbFree(db, cur->azColl[i]);
		}
		sqlite3DbFree(db, cur);
	}
	if (new_tuple != NULL) {
		say_debug("%s(): new_tuple != NULL", __func__);
		bool ok;
		SIndex *index = get_trntl_index_from_tuple(
			new_tuple, db, NULL, ok);
		if (!index) {
			say_debug("%s(): error while getting index "
				  "from tuple", __func__);
			return;
		}
		if (sqlite3SchemaToIndex(db, index->pSchema)) {
			idxHash = &db->aDb[1].pSchema->idxHash;
		}
		Table *table = index->pTable;
		if (index->is_autoincrement)
			table->tabFlags |= TF_Autoincrement;
		if (table->pIndex == NULL) {
			table->pIndex = index;
		} else {
			SIndex *last = table->pIndex;
			for (; last->pNext; last = last->pNext) {}
			last->pNext = index;
		}
		sqlite3HashInsert(idxHash, index->zName, index);
		state_add_index(state, index);
	}
}

/**
 * Call every time when _index is modified. This function
 * doesn't do any updates but creating new trigger on commiting
 * this _index updates.
 */
static void
on_replace_index(struct trigger *trigger, void *event)
{
	say_debug("%s():", __func__);
	struct txn *txn = (struct txn *) event;
	struct trigger *on_commit = (struct trigger *)
		region_calloc_object_xc(&fiber()->gc, struct trigger);
	trigger_create(on_commit, on_commit_index, trigger->data, NULL);
	txn_on_commit(txn, on_commit);
}

/**
 * Calls every time when _trigger is updated and this
 * changes are commited. It applies commited changes to
 * sqlite schema.
 */
static void
on_commit_trigger(struct trigger *trigger, void *event)
{
	say_debug("%s(): _trigger commited", __func__);
	struct txn *txn = (struct txn *) event;
	struct txn_stmt *stmt = txn_last_stmt(txn);
	struct tuple *old_tuple = stmt->old_tuple;
	struct tuple *new_tuple = stmt->new_tuple;
	if (old_tuple != NULL && new_tuple == NULL) {
		say_debug("%s(): _trigger old_tuple != NULL "
			  "and new_tuple == NULL", __func__);
		return;
	}
	else if (new_tuple != NULL) {
		say_debug("%s(): _trigger new_tuple != NULL "
			  "and  old_tuple == NULL", __func__);

		const char *xsql_field = (char *) tuple_field(new_tuple, 5);
		uint32_t stmt_len;
		const char *z_sql = mp_decode_str(&xsql_field, &stmt_len);

		sqlite3_stmt *sqlite3_stmt;
		sqlite3 *db = (sqlite3 *)trigger->data;
		Trigger *pTrigger;
		Vdbe *v;
		const char *pTail;
		int rc = sqlite3_prepare_v2(
			db, z_sql, stmt_len, &sqlite3_stmt, &pTail);
		if (rc) {
			say_debug("%s(): error while parsing "
				  "create statement for trigger",
				  __func__);
			return;
		}

		const char *tid_opt = tuple_field(new_tuple, 1);

		if (!db->init.busy) {
			v = (Vdbe *)sqlite3_stmt;
			(void) rc;
			pTrigger = sqlite3TriggerDup(
				db, v->pParse->pNewTrigger, 0);
			Trigger *pLink = pTrigger;
			Hash *trigHash = &pTrigger->pSchema->trigHash;
			pTrigger->tid = mp_decode_uint(&tid_opt);
			pTrigger = (Trigger *) sqlite3HashInsert(
				trigHash, pTrigger->zName, (void *)pTrigger);
			if(pTrigger){
				db->mallocFailed = 1;
			} else if(pLink->pSchema == pLink->pTabSchema) {
				Table *pTab;
				pTab = (Table *)sqlite3HashFind(
					&pLink->pTabSchema->tblHash,
					pLink->table);
				assert( pTab!=0 );
				pLink->pNext = pTab->pTrigger;
				pTab->pTrigger = pLink;
			}

		}
		sqlite3_finalize(sqlite3_stmt);
	}
}

/**
 * Call every time when _index is modified. This function
 * doesn't do any updates but creates a new trigger to be invoked on
 * transaction commit.
 */
static void
on_replace_trigger(struct trigger *trigger, void *event)
{
	say_debug("%s():", __func__);
	struct txn *txn = (struct txn *) event;
	struct trigger *on_commit = (struct trigger *)
		region_calloc_object_xc(&fiber()->gc, struct trigger);
	trigger_create(on_commit, on_commit_trigger, trigger->data, NULL);
	txn_on_commit(txn, on_commit);
}

static int
xsql_init(sqlite3 *db)
{
	sql_tarantool_state *state = (sql_tarantool_state *)malloc(
		sizeof (*state));

	if (state == NULL) {
		db->mallocFailed = 1;
		return SQLITE_NOMEM;
	}

	assert(db->tarantool.state == NULL);
	db->tarantool.state = state;
	state->cursors = NULL;
	state->cnt_cursors = 0;
	state->indices = NULL;
	state->cnt_indices = 0;

	/* _space */
	trigger_create(&state->space_on_replace,
		       on_replace_space, db, NULL);

	rlist_add_tail_entry(&space_cache_find(BOX_SPACE_ID)->on_replace,
			     &state->space_on_replace, link);

	/* _index */
	trigger_create(&state->index_on_replace,
		       on_replace_index, db, NULL);

	rlist_add_tail_entry(&space_cache_find(BOX_INDEX_ID)->on_replace,
			     &state->index_on_replace, link);

	/* _trigger */
	trigger_create(&state->trigger_on_replace,
		       on_replace_trigger, db, NULL);

	rlist_add_tail_entry(&space_cache_find(BOX_TRIGGER_ID)->on_replace,
			     &state->trigger_on_replace, link);

	return SQLITE_OK;
}

static void
xsql_finalize(sqlite3 *db)
{
	sql_tarantool_state *state = db->tarantool.state;

	rlist_del((rlist *) &state->space_on_replace);
	rlist_del((rlist *) &state->index_on_replace);
	rlist_del((rlist *) &state->trigger_on_replace);
	free(state);
	db->tarantool.state = NULL;
}

/* G L O B A L   O P E R A T I O N S */

static char *
make_msgpack_from_table(sqlite3 *db, const Table *table, int &size,
			const char *crt_stmt) {
	char *msg_data, *it;
	int space_id = get_space_id_from(table->tnum);
	int msg_size = 5 + mp_sizeof_uint(space_id);
	struct credentials *cred = current_user();
	const char *engine = "memtx";
	int name_len = strlen("name");
	int type_len = strlen("type");
	int engine_len = strlen(engine);
	int temporary_len = strlen("temporary");
	int view_len = strlen("is_view");
	int table_name_len = strlen(table->zName);
	int crt_stmt_len = crt_stmt ? strlen(crt_stmt) : 0;
	int stmt_len = strlen("sql");
	int has_crt_stmt = !!crt_stmt;
	Column *cur;
	int i;
	bool is_temp = sqlite3SchemaToIndex(db, table->pSchema);
	bool is_view = (table->pSelect != 0);
	int flags_cnt = (int)is_temp + is_view + has_crt_stmt;
	msg_size += mp_sizeof_uint(cred->uid);
	msg_size += mp_sizeof_str(table_name_len);
	msg_size += mp_sizeof_str(engine_len);
	msg_size += mp_sizeof_uint(0);
	msg_size += mp_sizeof_map(flags_cnt);
	//size of flags
	if (is_temp) {
		msg_size += mp_sizeof_str(temporary_len)
			+ mp_sizeof_bool(true);
	}
	if (is_view) {
		msg_size += mp_sizeof_str(view_len)
			+ mp_sizeof_bool(true);
	}
	if (crt_stmt) {
		msg_size += mp_sizeof_str(stmt_len)
				+ mp_sizeof_str(crt_stmt_len);
	}
	//sizeof parts
	msg_size += 5;
	msg_size += 5; // array of maps
	msg_size += (mp_sizeof_map(2) + mp_sizeof_str(name_len) +\
		mp_sizeof_str(type_len)) * table->nCol;
	for (i = 0; i < table->nCol; ++i) {
		cur = table->aCol + i;
		msg_size += mp_sizeof_str(strlen(cur->zName));
		msg_size += mp_sizeof_str(MAX_TYPE_LEN);
	}
	msg_data = new char[msg_size];
	it = msg_data;
	it = mp_encode_array(it, 7);
	it = mp_encode_uint(it, space_id); // space id
	it = mp_encode_uint(it, cred->uid); // owner id
	it = mp_encode_str(it, table->zName, table_name_len); // space name
	it = mp_encode_str(it, engine, engine_len); // space engine
	it = mp_encode_uint(it, 0); // field count

	// flags
	it = mp_encode_map(it, flags_cnt);
	if (is_temp) {
		it = mp_encode_str(it, "temporary", temporary_len);
		it = mp_encode_bool(it, true);
	}
	if (is_view) {
		it = mp_encode_str(it, "is_view", view_len);
		it = mp_encode_bool(it, true);
	}
	if (crt_stmt) {
		it = mp_encode_str(it, "sql", stmt_len);
		it = mp_encode_str(it, crt_stmt, crt_stmt_len);
	}
	it = mp_encode_array(it, table->nCol);
	for (i = 0; i < table->nCol; ++i) {
		cur = table->aCol + i;
		it = mp_encode_map(it, 2);
		it = mp_encode_str(it, "name", name_len);
		it = mp_encode_str(it, cur->zName,
				   strlen(cur->zName));
		it = mp_encode_str(it, "type", type_len);
		field_type ftp;
		ftp = get_tarantool_type_from_sql_aff(
			cur->affinity);
		if (ftp == FIELD_TYPE_ANY) {
			size = 0;
			delete[] msg_data;
			return NULL;
		}
		size_t len = strlen(field_type_strs[ftp]);
		assert(len <= MAX_TYPE_LEN);
		it = mp_encode_str(it, field_type_strs[ftp], len);
	}
	size = it - msg_data;
	return msg_data;
}

static char *
make_msgpack_from_index(const SIndex *index, int &size)
{
	char *msg_data = NULL;
	int msg_size = 5;
	int space_id = get_space_id_from(index->tnum);
	int index_id = get_index_id_from(index->tnum) % 15;
	int name_len = strlen(index->zName);
	int opt_count = 1; // 'unique' opt always here
	if (index->is_autoincrement)
		opt_count++;

	const char *type = "TREE";
	int type_len = strlen(type);
	msg_size += mp_sizeof_uint(space_id);
	msg_size += mp_sizeof_uint(index_id);
	msg_size += mp_sizeof_str(name_len);
	msg_size += mp_sizeof_str(type_len);
	msg_size += mp_sizeof_map(opt_count);
	msg_size += mp_sizeof_str(strlen("unique"));
	msg_size += mp_sizeof_bool(true); //sizes of true and false are equal
	msg_size += mp_sizeof_str(strlen("is_autoincrement"));
	msg_size += mp_sizeof_bool(true);
	msg_size += mp_sizeof_array(index->nKeyCol);
	for (int i = 0; i < index->nKeyCol; ++i) {
		msg_size += mp_sizeof_array(2);
		msg_size += mp_sizeof_uint(index->aiColumn[i]);
		msg_size += mp_sizeof_str(MAX_TYPE_LEN);
	}
	msg_data = new char[msg_size];
	char *it = msg_data;
	it = mp_encode_array(it, 6);
	it = mp_encode_uint(it, space_id);
	it = mp_encode_uint(it, index_id);
	it = mp_encode_str(it, index->zName, name_len);
	it = mp_encode_str(it, type, type_len);
	it = mp_encode_map(it, opt_count);
		it = mp_encode_str(it, "unique", strlen("unique"));
	it = mp_encode_bool(it, index->idxType != 0);
	if (index->is_autoincrement) {
		it = mp_encode_str(it, "is_autoincrement",
				   strlen("is_autoincrement"));
		it = mp_encode_bool(it, true);
	}
	it = mp_encode_array(it, index->nKeyCol);
	for (int i = 0; i < index->nKeyCol; ++i) {
		int col = index->aiColumn[i];
		int affinity = index->pTable->aCol[col].affinity;
		it = mp_encode_array(it, 2);
		it = mp_encode_uint(it, col);
		field_type ftp = get_tarantool_type_from_sql_aff(affinity);
		if (ftp == FIELD_TYPE_ANY) {
			size = 0;
			delete[] msg_data;
			return NULL;
		}
		size_t len = strlen(field_type_strs[ftp]);
		assert(len <= MAX_TYPE_LEN);
		it = mp_encode_str(it, field_type_strs[ftp], len);
	}
	size = it - msg_data;
	return msg_data;
}

static int
get_max_id_of_space(int cspace_id)
{
	int id_max = -1;
	char key[2], *key_end = mp_encode_array(key, 0);
	void *argv[1];
	argv[0] = (void *)&id_max;
	auto callback =
		[](box_tuple_t *tpl, int, void **argv) -> int {
			const char *data = box_tuple_field(tpl, 0);
			MValue space_id = MValue::FromMSGPuck(&data);
			int *id_max = (int *)argv[0];
			if ((int64_t)space_id.GetUint64() > *id_max)
				*id_max = space_id.GetUint64();
			return 0;
		};
	SpaceIterator space_iterator(1, argv, callback, cspace_id, 0, key, key_end);
	space_iterator.IterateOver();
	return id_max;
}

static int
get_max_id_of_index(int space_id)
{
	int id_max = -2;
	char key[128], *key_end = mp_encode_array(key, 1);
	key_end = mp_encode_uint(key_end, space_id);
	void *argv[1];
	argv[0] = (void *)&id_max;
	auto callback=
		[](box_tuple_t *tpl, int, void **argv) -> int {
			const char *data = box_tuple_field(tpl, 1);
			MValue index_id = MValue::FromMSGPuck(&data);
			int *id_max = (int *)argv[0];
			if ((int64_t)index_id.GetUint64() > *id_max)
				*id_max = index_id.GetUint64();
			return 0;
		};
	SpaceIterator space_iterator(1, argv, callback, BOX_INDEX_ID, 0,
				     key, key_end, ITER_EQ);
	space_iterator.IterateOver();
	if (space_iterator.IsOpen() && space_iterator.IsEnd() && (id_max < 0))
		id_max = -1;
	return id_max;
}

static int
get_max_id_of_trigger(int space_id)
{
	int id_max = -2;
	char key[128], *key_end = mp_encode_array(key, 1);
	key_end = mp_encode_uint(key_end, space_id);
	void *argv[1];
	argv[0] = (void *)&id_max;
	auto callback=
		[](box_tuple_t *tpl, int, void **argv) -> int {
			const char *data = box_tuple_field(tpl, 1);
			MValue index_id = MValue::FromMSGPuck(&data);
			int *id_max = (int *)argv[0];
			if ((int64_t)index_id.GetUint64() > *id_max)
				*id_max = index_id.GetUint64();
			return 0;
		};
	SpaceIterator space_iterator(1, argv, callback, BOX_TRIGGER_ID, 0,
				     key, key_end, ITER_EQ);
	space_iterator.IterateOver();
	if (space_iterator.IsOpen() && space_iterator.IsEnd() && (id_max < 0))
		id_max = -1;
	return id_max;
}

static int
insert_new_table_as_space(sqlite3 *db, Table *table, const char *crt_stmt)
{
	char *msg_data;
	int msg_size;
	int id_max = get_max_id_of_space();
	id_max = MAX(id_max, BOX_SYSTEM_ID_MAX + 1);
	int rc;
	if (id_max < 0) {
		say_debug("%s(): error while getting max id", __func__);
		return -1;
	}
	id_max += 5;
	table->tnum = make_space_id(id_max);
	msg_data = make_msgpack_from_table(db, (const Table *)table, msg_size, crt_stmt);
	rc = box_insert(BOX_SPACE_ID, msg_data, msg_data + msg_size, NULL);
	delete[] msg_data;
	return rc;
}

static int
insert_new_view_as_space(sqlite3 *db, Table *table, const char *crt_stmt) {
	char *msg_data;
	int msg_size;
	int id_max = get_max_id_of_space();
	id_max = MAX(id_max, BOX_SYSTEM_ID_MAX + 1);
	int rc;
	if (id_max < 0) {
		say_debug("%s(): error while getting max id", __func__);
		return -1;
	}
	id_max += 5;
	table->tnum = make_space_id(id_max);
	msg_data = make_msgpack_from_table(db, (const Table *)table, msg_size, crt_stmt);
	rc = box_insert(BOX_SPACE_ID, msg_data, msg_data + msg_size, NULL);
	delete[] msg_data;
	return rc;
}

static int
insert_new_sindex_as_index(SIndex *index)
{
	char *msg_data = NULL;
	int msg_size;
	int space_id = get_space_id_from(index->pTable->tnum);
	int id_max = get_max_id_of_index(space_id);
	int rc;
	if (id_max < -1) {
		say_debug("%s(): error while getting max id",
			  __func__);
		return -1;
	}
	id_max++;
	index->tnum = make_index_id(space_id, id_max);
	msg_data = make_msgpack_from_index(index, msg_size);
	rc = box_insert(BOX_INDEX_ID, msg_data, msg_data + msg_size, NULL);
	delete[] msg_data;
	return rc;
}

static int
insert_trigger(sqlite3 *db, Trigger *trigger, char *crt_stmt)
{
	uint32_t iDb = sqlite3SchemaToIndex(db, trigger->pSchema);

	Hash *tblHash = &trigger->pSchema->tblHash;
	Table *table = (Table *)sqlite3HashFind(tblHash, trigger->table);
	char crt_stmt_full[BOX_SQL_STMT_MAX];

	if (!table) {
		say_debug("%s(): error while getting id of space",
			  __func__);
		return 1;
	}
	uint32_t space_id = get_space_id_from(table->tnum);

	uint32_t max_id = get_max_id_of_trigger(space_id);
	uint32_t new_id = max_id + 1;

	static const char *create = "CREATE TRIGGER %s";

	int full_stmt_len = snprintf(crt_stmt_full, BOX_SQL_STMT_MAX,
				     create, crt_stmt);

	bool is_temp = (iDb == 1);

	uint32_t temporary_len = 9; //9 = strlen("temporary")
	uint32_t rec_size = 0;
	rec_size += mp_sizeof_array(8);
	rec_size += mp_sizeof_uint(space_id);
	rec_size += mp_sizeof_uint(new_id);
	rec_size += mp_sizeof_uint(0); // owner id
	rec_size += mp_sizeof_str(strlen(trigger->zName));
	rec_size += mp_sizeof_str(strlen(trigger->table));
	rec_size += mp_sizeof_str(full_stmt_len);
	rec_size += mp_sizeof_uint(0); // setuid
	rec_size += mp_sizeof_map(1);
	rec_size += mp_sizeof_str(temporary_len);
	rec_size += mp_sizeof_bool(is_temp);

	char *new_tuple = new char[rec_size];
	char *it = new_tuple;

	it = mp_encode_array(it, 8);
	it = mp_encode_uint(it, space_id);
	it = mp_encode_uint(it, new_id);
	it = mp_encode_uint(it, 0); // owner id
	it = mp_encode_str(it, trigger->zName, strlen(trigger->zName));
	it = mp_encode_str(it, trigger->table, strlen(trigger->table));
	it = mp_encode_str(it, crt_stmt_full, full_stmt_len);
	it = mp_encode_uint(it, 0); // setuid
	it = mp_encode_map(it, 1);
	it = mp_encode_str(it, "temporary", temporary_len);
	it = mp_encode_bool(it, is_temp);

	box_insert(BOX_TRIGGER_ID, new_tuple, it, NULL);

	delete[] new_tuple;

	return 0;
}

static Table *
get_trntl_table_from_tuple(box_tuple_t *tpl, sqlite3 *db,
			   Schema *pSchema, bool *is_temp,
			   bool *is_view, bool is_delete)
{
	int cnt = box_tuple_field_count(tpl);
	if (cnt != 7) {
		say_debug("%s(): box_tuple_field_count is %d,  "
			  "expected 7", __func__, cnt);
		return NULL;
	}
	sqlite3 *db_alloc = 0;
	Hash *tblHash = &db->aDb[0].pSchema->tblHash;
	char zName[256];
	memset(zName, 0, sizeof(zName));

	const char *data = box_tuple_field(tpl, 0);
	int type = (int)mp_typeof(*data);
	MValue tbl_id = MValue::FromMSGPuck(&data);
	if (tbl_id.GetType() != MP_UINT) {
		say_debug("%s(): field[0] in tuple in SPACE must be uint, "
			  "but is %d", __func__, type);
		return NULL;
	}

	data = box_tuple_field(tpl, 2);
	type = (int)mp_typeof(*data);
	if (type != MP_STR) {
		say_debug("%s(): field[2] in tuple in SPACE must be string, "
			  "but is %d", __func__, type);
		return NULL;
	} else {
		size_t len;
		MValue buf = MValue::FromMSGPuck(&data);
		buf.GetStr(&len);
		memcpy(zName, buf.GetStr(), len);
	}

	Table *new_table = (Table *)
		sqlite3DbMallocZero(db_alloc, sizeof(Table));
	new_table->zName = sqlite3DbStrDup(db_alloc, zName);
	new_table->iPKey = -1;
	new_table->pSchema = pSchema;
	new_table->nRef = 1;
	new_table->nRowLogEst = 200;
	auto table = unique_ptr_with_deletor( new_table, [=] (Table *ptr) {
		if (!ptr) return;
		sqlite3DbFree(db, ptr->zName);
		sqlite3DbFree(db, ptr);
	});
	char key[128], *key_end;
	key_end = mp_encode_array(key, 1);
	key_end = mp_encode_uint(key_end, tbl_id.GetUint64());
	int index_len = 200;
	void *argv[1];
	argv[0] = (void *)&index_len;
	SpaceIterator::SIteratorCallback callback =
	[](box_tuple_t *tpl, int, void **argv) -> int
	{
		int *index_len = (int *)argv[0];
		const char *data = box_tuple_field(tpl, 0);
		MValue space_id = MValue::FromMSGPuck(&data);
		data = box_tuple_field(tpl, 1);
		MValue index_id = MValue::FromMSGPuck(&data);
		int tmp = box_index_len(space_id.GetUint64(),
			index_id.GetUint64());
		if (tmp > *index_len) *index_len = tmp;
		return 1;
	};
	SpaceIterator iterator(1, argv, callback, BOX_INDEX_ID,
		0, key, key_end, ITER_EQ);
	iterator.IterateOver();
	table->nRowLogEst = index_len;
	table->szTabRow = ESTIMATED_ROW_SIZE;
	table->tnum = make_space_id(tbl_id.GetUint64());
	if (db->mallocFailed) {
		say_debug("%s(): error while allocating memory "
			  "for table", __func__);
		return NULL;
	}
	table->pSchema = pSchema;
	table->iPKey = -1;
	table->tabFlags = TF_WithoutRowid | TF_HasPrimaryKey;
	//Get flags
	if (is_temp)
		*is_temp = false;
	if (is_view)
		*is_view = false;
	bool local_is_view = false;

	data = box_tuple_field(tpl, 5);
	uint32_t map_size = mp_typeof(*data) == MP_MAP ?
		mp_decode_map(&data) : 0;
	Vdbe *v;
	Table *pstmtTable = NULL;
	for (uint32_t i = 0; i < map_size; ++i) {
		MValue name = MValue::FromMSGPuck(&data);
		MValue value = MValue::FromMSGPuck(&data);
		const char *pname = name.GetStr();
		if (!strcmp(pname, "temporary") && value.GetBool()) {
			if (is_temp) *is_temp = true;
		}
		if (!strcmp(pname, "is_view") && value.GetBool()) {
			if (is_view)
				*is_view = true;
			local_is_view = true;
			/* View has not got any tableFlags in sqlite */
			table->tabFlags = 0;
		}
		if (!strcmp(pname, "sql") && !is_delete) {
			/* we need not to parse crt_stmt_in case of deleting
			 * view
			 **/
			sqlite3_stmt *stmt;
			const char *pTail;
			const char *z_sql = value.GetStr();
			uint32_t len = strlen(z_sql);
			int rc;
			if (!local_is_view) {
				//this is TABLE
				rc = sqlite3_prepare_v3_taran(
					db, z_sql, len, &stmt, &pTail);
				if (rc != SQLITE_OK) {
					say_debug("%s(): error while "
						  "parsing create "
						  "statement for table",
						  __func__);
					return NULL;
				}
				v = (Vdbe *)stmt;
				//all that we need is on callbacks stack
				void **argv;
				rc = sqlite3VdbeNestedCallbackByID(
					v, "creating space",
					NULL, NULL, &argv);
				if (rc < 0) {
					say_debug("%s(): creating space "
						  "callback was not found",
						  __func__);
					sqlite3_finalize(stmt);
					return NULL;
				}
				pstmtTable = make_deep_copy_Table(
					(Table *)argv[2], db_alloc);
				sqlite3_finalize(stmt);
				pstmtTable->tnum = table->tnum;
				pstmtTable->pSchema = pSchema;
				pstmtTable->iPKey = -1;
				pstmtTable->tabFlags =
					TF_WithoutRowid | TF_HasPrimaryKey;
				pstmtTable->nRowLogEst = table->nRowLogEst;
				pstmtTable->szTabRow = table->szTabRow;
				continue;
			} else {
				//this is VIEW
				rc = sqlite3_prepare_v2(
					db, z_sql, len, &stmt, &pTail);
				if (rc != SQLITE_OK) {
					say_debug("%s(): error while "
						  "parsing create "
						  "statement for view",
						  __func__);
					return NULL;
				}
				if (!db->init.busy) {
					v = (Vdbe *)stmt;
					(void) rc;
					Parse *pParse = v->pParse;
					table->pSelect = sqlite3SelectDup(
						0,
						pParse->pNewTable->pSelect,
						0);
				} else {
					/* In init process we need to
					 * update tnum in inmemory
					 * representation.
					 **/
					pstmtTable = (Table *)sqlite3HashFind(
						tblHash, table->zName);
					pstmtTable->tnum = table->tnum;
				}
				sqlite3_finalize(stmt);
			}
		}
	}
	/* we can't return immediately above because we need to walk
	 * through all space opts */
	if (pstmtTable)
		return pstmtTable;
	/* Get space format */
	data = box_tuple_field(tpl, 6);
	uint32_t len = mp_decode_array(&data);

	auto cols = unique_ptr_with_deletor(
		(Column *)sqlite3DbMallocZero(db_alloc, len * sizeof(Column)),
		[] (Column *ptr) {
			sqlite3DbFree(0, ptr);
		}
	);
	memset(cols.get(), 0, sizeof(Column) * len);
	int nCol = 0;
	auto free_cols_content = [&](){
		for (int i = 0; i < nCol; ++i) {
			Column *cur = cols.get() + i;
			sqlite3DbFree(db_alloc, cur->zName);
			sqlite3DbFree(db_alloc, cur->zType);
		}
	};
	for (uint32_t i = 0; i < len; ++i) {
		map_size = mp_decode_map(&data);
		MValue colname, coltype;
		if (map_size != 2) {
			say_debug("%s(): map_size not equal 2, "
				  "but %u", __func__, map_size);
			free_cols_content();
			return NULL;
		}
		for (uint32_t j = 0; j < map_size; ++j) {
			MValue key = MValue::FromMSGPuck(&data);
			MValue val = MValue::FromMSGPuck(&data);
			if (key.GetType() != MP_STR ||
			    val.GetType() != MP_STR)
			{
				say_debug("%s(): expected string format",
					  __func__);
				free_cols_content();
				return NULL;
			}
			char c = key.GetStr()[0];
			if ((c == 'n') || (c == 'N')) {
				//name
				colname = val;
			} else if ((c == 't') || (c == 'T')) {
				//type
				coltype = val;
			} else {
				say_debug("%s(): unknown string in "
					  "space_format", __func__);
				free_cols_content();
				return NULL;
			}
		}
		if (colname.IsEmpty() || coltype.IsEmpty()) {
			say_debug("%s(): both name and type "
				  "must be present", __func__);
		}
		const char *xsql_type;
		const char *type = coltype.GetStr();
		int affinity;
		if (strcasecmp(type, "number") == 0) {

			affinity = SQLITE_AFF_REAL;
			xsql_type = "REAL";
		} else if (strcasecmp(type, "int") == 0 ||
			   strcasecmp(type, "integer") == 0 ||
			   strcasecmp(type, "uint") == 0 ||
			   strcasecmp(type, "unsigned") == 0 ||
			   strcasecmp(type, "num") == 0) {

			affinity = SQLITE_AFF_INTEGER;
			xsql_type = "INT";
		} else if (strcasecmp(type, "string") == 0 ||
			   strcasecmp(type, "str") == 0) {

			affinity = SQLITE_AFF_TEXT;
			xsql_type = "TEXT";
		} else {

			affinity = SQLITE_AFF_BLOB;
			xsql_type = "BLOB";
		}
		Column *cur = cols.get() + nCol++;
		size_t len;
		colname.GetStr(&len);
		cur->zName = (char *)sqlite3DbMallocZero(
			db_alloc, len + 1);
		memset(cur->zName, 0, len + 1);
		memcpy(cur->zName, colname.GetStr(), len);

		len = strlen(xsql_type);
		cur->zType = (char *)sqlite3DbMallocZero(
			db_alloc, len + 1);
		memset(cur->zType, 0, len + 1);
		memcpy(cur->zType, xsql_type, len);
		cur->affinity = affinity;
	}
	table->aCol = cols.release();
	table->nCol = nCol;
	return table.release();
}

static SIndex *
get_trntl_index_from_tuple(box_tuple_t *index_tpl, sqlite3 *db,
			   Table *table, bool &ok) {
	ok = false;

	int cnt = box_tuple_field_count(index_tpl);
	if (cnt != 6) {
		say_debug("%s(): box_tuple_field_count not equal 6, "
			  "but %d, for next index", __func__, cnt);
		return NULL;
	}

	/* SPACE ID */

	const char *data = box_tuple_field(index_tpl, 0);
	int type = (int)mp_typeof(*data);
	MValue space_id = MValue::FromMSGPuck(&data);
	if (space_id.GetType() != MP_UINT) {
		say_debug("%s(): field[0] in tuple in INDEX "
			  "must be uint, but is %d", __func__, type);
		return NULL;
	}

	if (!table) {
		Schema *pSchema = db->aDb[0].pSchema;
		char key[256], *key_end;
		key_end = mp_encode_array(key, 1);
		key_end = mp_encode_uint(key_end, space_id.GetUint64());
		bool is_temp = false;
		void *params[2];
		MValue space_name;
		params[0] = (void *)&is_temp;
		params[1] = (void *)&space_name;
		SpaceIterator::SIteratorCallback callback =
			[] (box_tuple_t *tpl, int, void **argv) -> int {
				bool *is_temp = (bool *)(argv[0]);
				MValue *space_name = (MValue *)argv[1];
				const char *data;
				data = box_tuple_field(tpl, 2);
				*space_name = MValue::FromMSGPuck(&data);
				if (box_tuple_field_count(tpl) != 7)
					return 0;
				data = box_tuple_field(tpl, 5);
				uint32_t map_size = mp_typeof(*data) == MP_MAP ? mp_decode_map(&data) : 0;
				for (uint32_t i = 0; i < map_size; ++i) {
					MValue name = MValue::FromMSGPuck(&data);
					MValue value = MValue::FromMSGPuck(&data);
					if (!strcmp(name.GetStr(), "temporary") &&
					    value.GetBool()) {
						*is_temp = true;
						break;
					}
				}
				return 0;
			};
		SpaceIterator iterator(2, params, callback, BOX_SPACE_ID,
			               0, key, key_end, ITER_EQ);
		iterator.IterateOver();
		if (is_temp) {
			pSchema = db->aDb[1].pSchema;
		}
		table = (Table *)sqlite3HashFind(
			&pSchema->tblHash, space_name.GetStr());
		if (!table) {
			say_debug("%s(): space with id %llu "
				  "was not found", __func__,
				  space_id.GetUint64());
			return NULL;
		}
	}

	uint32_t tbl_id = get_space_id_from(table->tnum);
	if (space_id.GetUint64() != tbl_id) {
		ok = true;
		return NULL;
	}

	char *extra = NULL;
	int extra_sz = 0;
	char zName[256];

	/* INDEX ID */

	data = box_tuple_field(index_tpl, 1);
	type = (int)mp_typeof(*data);
	MValue index_id = MValue::FromMSGPuck(&data);
	if (index_id.GetType() != MP_UINT) {
		say_debug("%s(): field[1] in tuple in INDEX must be uint, "
			  "but is %d", __func__, type);
		return NULL;
	}

	/* INDEX NAME */

	data = box_tuple_field(index_tpl, 2);
	type = (int)mp_typeof(*data);
	if (type != MP_STR) {
		say_debug("%s(): field[2] in tuple in INDEX "
			  "must be string, but is %d",
			  __func__, type);
		return NULL;
	} else {
		memset(zName, 0, 256);
		size_t len = 0;
		MValue buf = MValue::FromMSGPuck(&data);
		buf.GetStr(&len);
		sprintf(zName, "%d_%d_", (int)space_id.GetUint64(),
			(int)index_id.GetUint64());
		memcpy(zName + strlen(zName), buf.GetStr(), len);
		extra_sz += strlen(zName) + 1;
	}

	auto index = unique_ptr_with_deletor(
		sqlite3AllocateIndexObject(db, table->nCol,
					   extra_sz, &extra),
		[=] (SIndex *ptr) {
			sqlite3DbFree(db, ptr);
		}
	);
	if (db->mallocFailed) {
		say_debug("%s(): failed to allocate memory "
			  "for index", __func__);
		return NULL;
	}
	index->pTable = table;
	index->pSchema = table->pSchema;
	index->isCovering = 1;
	index->noSkipScan = 1;
	if (index_id.GetUint64()) {
		index->idxType = 0;
	} else {
		index->idxType = 2;
	}
	index->tnum = make_index_id(space_id.GetUint64(),
				    index_id.GetUint64());
	index->zName = extra;
	{
		int len = strlen(zName);
		memcpy(index->zName, zName, len);
		index->zName[len] = 0;
	}

	/* SORT ORDER */

	index->aSortOrder[0] = 0;
	index->szIdxRow = ESTIMATED_ROW_SIZE;
	index->nColumn = table->nCol;
	index->onError = OE_None;
	for (int j = 0; j < table->nCol; ++j) {
		index->azColl[j] = reinterpret_cast<char *>(
			sqlite3DbMallocZero(db, sizeof(char) * (
					    strlen("BINARY") + 1)));
		memcpy(index->azColl[j], "BINARY", strlen("BINARY"));
	}
	auto free_index_azColls = [&]() {
		for (int j = 0; j < table->nCol; ++j) {
			sqlite3DbFree(db, index->azColl[j]);
		}
	};

	/* TYPE */

	data = box_tuple_field(index_tpl, 3);
	type = (int)mp_typeof(*data);
	if (type != MP_STR) {
		say_debug("%s(): field[3] in tuple in INDEX must "
			  "be string, but is %d",
			  __func__, type);
		free_index_azColls();
		return NULL;
	} else {
		MValue buf = MValue::FromMSGPuck(&data);
		if ((buf.GetStr()[0] == 'T') || (buf.GetStr()[0] == 't')) {
			index->bUnordered = 0;
		} else {
			index->bUnordered = 1;
		}
	}

	/* UNIQUE */

	data = box_tuple_field(index_tpl, 4);
	int map_size = mp_typeof(*data) == MP_MAP ? mp_decode_map(&data) : 0;
	if (map_size > 3) {
		say_debug("%s(): field[4] map size in INDEX "
			  "must be <= 3, but is %u", __func__, map_size);
		free_index_azColls();
		return NULL;
	}
	MValue key, value;
	for (int j = 0; j < map_size; ++j) {
		key = MValue::FromMSGPuck(&data);
		value = MValue::FromMSGPuck(&data);
		if (key.GetType() != MP_STR)
			continue;
		if (value.GetType() != MP_BOOL)
			continue;

		if (strcasecmp(key.GetStr(), "unique") == 0) {

			if (value.GetBool() && index->idxType != 2)
				index->idxType = 1;
		} else if (strcasecmp(key.GetStr(), "is_autoincrement") == 0) {

			say_debug("%s(): found index with "
				  "autoincrement: %s", __func__, index->zName);
			index->is_autoincrement = 1;
		}
	}

	/* INDEX FORMAT */

	data = box_tuple_field(index_tpl, 5);
	MValue idx_cols = MValue::FromMSGPuck(&data);
	if (idx_cols.GetType() != MP_ARRAY) {
		say_debug("%s(): field[5] in INDEX must be an array, "
			  "but type is %d", __func__, idx_cols.GetType());
		free_index_azColls();
		return NULL;
	}
	index->nKeyCol = idx_cols.Size();
	if (index->nKeyCol > table->nCol) {
		index->nKeyCol = 0;
		ok = true;
		return index.release();
	}
	for (int j = 0, sz = idx_cols.Size(); j < sz; ++j) {
		i16 num = idx_cols[j][0][0]->GetUint64();
		index->aiColumn[j] = num;
	}
	for (int j = 0, start = idx_cols.Size(); j < table->nCol; ++j) {
		bool used = false;
		for (uint32_t k = 0, sz = idx_cols.Size(); k < sz; ++k) {
			if (index->aiColumn[k] == j) {
				used = true;
				break;
			}
		}
		if (used)
			continue;
		index->aiColumn[start++] = j;
	}

	for (int i = 0; i < index->nKeyCol; ++i)
		index->aiRowLogEst[i] = table->nRowLogEst;

	ok = true;
	return index.release();
}

static int
drop_index(int space_id, int index_id) {
	int rc;
	char key[128], *key_end;
	key_end = mp_encode_array(key, 2);
	key_end = mp_encode_uint(key_end, space_id);
	key_end = mp_encode_uint(key_end, index_id);
	rc = box_delete(BOX_INDEX_ID, 0, key, key_end, NULL);
	if (rc) {
		say_debug("%s(): error = %s", __func__,
			  box_error_message(box_error_last()));
	}
	return rc;
}

static int
drop_all_indices(int space_id) {
	int rc;
	char key[128], *key_end;
	key_end = mp_encode_array(key, 1);
	key_end = mp_encode_uint(key_end, space_id);
	void *argv[2];
	int indices[15];
	int ind_cnt = 0;
	argv[0] = (void *)indices;
	argv[1] = (void *)&ind_cnt;
	SpaceIterator::SIteratorCallback callback;
	callback = [] (box_tuple_t *tpl, int, void **argv) -> int {
		int *indices = (int *)argv[0];
		int *ind_cnt = (int *)argv[1];
		const char *data = box_tuple_field(tpl, 1);
		MValue index_id = MValue::FromMSGPuck(&data);
		indices[(*ind_cnt)++] = index_id.GetUint64();
		return 0;
	};
	SpaceIterator iterator(
		3, argv, callback, BOX_INDEX_ID, 0, key, key_end, ITER_EQ);
	rc = iterator.IterateOver();
	for (--ind_cnt; ind_cnt >= 0; --ind_cnt) {
		rc = drop_index(space_id, indices[ind_cnt]);
		if (rc) return rc;
	}
	return rc;
}

/**
 * Function for joining sqlite schema and tarantool schema.
 */
static void
xsql_get_spaces(sqlite3 *db, char **pzErrMsg, Schema *pSchema,
	Hash *idxHash, Hash *tblHash) {
	(void)pzErrMsg;
	sql_tarantool_state *state = db->tarantool.state;
	bool must_be_temp = sqlite3SchemaToIndex(db, pSchema);

	char key[2], *key_end = mp_encode_array(key, 0);
	SpaceIterator space_iterator(WITHOUT_CALLBACK, BOX_SPACE_ID, 0, key, key_end);
	box_tuple_t *tpl = NULL;

	do {
		if (space_iterator.Next()) {
			say_debug("%s(): space_iterator return not 0", __func__);
			return;
		}
		if (space_iterator.IsEnd()) break;
		tpl = space_iterator.GetTuple();
		bool is_temp;
		bool is_view;
		auto table = unique_ptr_with_deletor(
			get_trntl_table_from_tuple(tpl, db, pSchema,
				                   &is_temp, &is_view),
			[=] (Table *ptr) {
				if (!ptr) return;
				int i;
				sqlite3DbFree(db, ptr->zName);
				for (i = 0; i < ptr->nCol; ++i) {
					sqlite3DbFree(db, ptr->aCol[i].zName);
					sqlite3DbFree(db, ptr->aCol[i].zType);
				}
				sqlite3DbFree(db, ptr->aCol);
				sqlite3DbFree(db, ptr);
			}
		);
		if (is_temp != must_be_temp) {
			continue;
		}
		if (table.get() == NULL) return;
		if (!strcmp(table->zName, "sqlite_master")) {
			continue;
		}
		if (!strcmp(table->zName, "sqlite_temp_master")) {
			continue;
		}
		if (! (db->init.busy && is_view) ) {
			/**
			  * For view insertion in inmemory representation made
			  * in sqlite3EndTable
			  */
			sqlite3HashInsert(tblHash, table->zName, table.get());
		}

		/* Indices */
		SpaceIterator index_iterator(
			WITHOUT_CALLBACK, BOX_INDEX_ID, 0, key, key_end);
		box_tuple_t *index_tpl = NULL;
		do {
			MValue key, value, idx_cols, index_id, space_id;
			SIndex *index = NULL;

			if (index_iterator.Next()) {
				say_debug("%s(): index_iterator "
					  "returned not 0 "
					  "for next index", __func__);
				goto badindex;
			}
			if (index_iterator.IsEnd()) {
				break;
			}
			index_tpl = index_iterator.GetTuple();
			bool ok;
			index = get_trntl_index_from_tuple(
				index_tpl, db, table.get(), ok);
			if (index == NULL) {
				if (ok) continue;
				say_debug("%s(): error while "
					  "getting index from tuple",
					  __func__);
				sqlite3HashInsert(
					tblHash, table->zName, NULL);
				return;
			}
			if (index->is_autoincrement)
				table->tabFlags |= TF_Autoincrement;

			sqlite3HashInsert(idxHash, index->zName, index);
			if (!table->pIndex) {
				table->pIndex = index;
			} else {
				SIndex *last = table->pIndex;
				for (; last->pNext; last = last->pNext) {}
				last->pNext = index;
			}

			state_add_index(state, index);

			continue;
badindex:
			sqlite3HashInsert(tblHash, table->zName, NULL);
			sqlite3HashInsert(idxHash, index->zName, NULL);
			if (index->aSortOrder) sqlite3DbFree(db, index->aSortOrder);
			if (index->aiColumn) sqlite3DbFree(db, index->aiColumn);
			if (index->azColl) {
				for (uint32_t j = 0; j < index->nColumn; ++j) {
					sqlite3DbFree(db, index->azColl[j]);
				}
				sqlite3DbFree(db, index->azColl);
			}
			if (index) sqlite3DbFree(db, index);
			return;
		} while (index_iterator.InProcess());
		table.release();
	} while (space_iterator.InProcess());
	return;
}

/**
 * Add triggers from _trigger to sqlite inmemory representation.
 */
static void
xsql_get_triggers(sqlite3 *db, Schema *pSchema,
		 Hash *tblHash, Hash *trigHash) {
	(void)tblHash;
	(void)trigHash;

	bool must_be_temp = sqlite3SchemaToIndex(db, pSchema);
	bool is_temp;
	char key[2], *key_end = mp_encode_array(key, 0);
	SpaceIterator trigger_iterator(
		WITHOUT_CALLBACK, BOX_TRIGGER_ID, 0, key, key_end);
	box_tuple_t *trigger_tpl = NULL;

	do {
		if (trigger_iterator.Next()) {
			say_debug("%s(): trigger_iterator "
				  "returned not 0", __func__);
			return;
		}
		if (trigger_iterator.IsEnd()) break;


		trigger_tpl = trigger_iterator.GetTuple();

		/* get create statement */
		const char *tid_opt = tuple_field(trigger_tpl, 1);

		uint32_t name_len, crt_stmt_len;
		const char *trig_name_field =
			(const char *) tuple_field(trigger_tpl, 3);
		const char *trig_name_tuple =
			mp_decode_str(&trig_name_field, &name_len);
		const char *crt_stmt_field =
			(const char *) tuple_field(trigger_tpl, 5);
		const char *crt_stmt_tuple =
			mp_decode_str(&crt_stmt_field, &crt_stmt_len);
		char *crt_stmt = new char[crt_stmt_len + 1];
		memcpy((void *) crt_stmt, (void *) crt_stmt_tuple, crt_stmt_len);
		crt_stmt[crt_stmt_len] = 0;
		char *trig_name = new char[name_len + 1];
		memcpy((void *) trig_name, (void *) trig_name_tuple, name_len);
		trig_name[name_len] = 0;

		const char *options = tuple_field(trigger_tpl, 7);
		static uint32_t temporary_len = 9; // 9 = strlen("temporary")
		uint32_t opt_size = mp_decode_map(&options);

		if (opt_size != 1) {
			say_debug("%s(): multiple options found",
				  __func__);
		}

		uint32_t key_len;
		const char *key = mp_decode_str(&options, &key_len);
		if (key_len == temporary_len &&
		    !memcmp(key, "temporary", temporary_len))
		{
			if (mp_typeof(*options) != MP_BOOL) {
				say_debug("%s(): temporary option "
					  "not BOOL", __func__);
				return;
			}
			is_temp = mp_decode_bool(&options);
		}
		else {
			say_debug("%s(): unexpected option", __func__);
			return;
		}
		if (is_temp == must_be_temp) {
			/*
			 * Insertion in sqlite inmemory representation
			 * will be made in sqlite3FinishTrigger.
			 * sqlite3FinishTrigger will be called in
			 * sqlite3_prepare_v2.
			 */

			sqlite3_stmt *sqlite3_stmt;
			uint32_t len = strlen(crt_stmt);
			Trigger *pTrigger;
			const char *pTail;
			int rc = sqlite3_prepare_v2(
				db, crt_stmt, len, &sqlite3_stmt, &pTail);
			if (rc) {
				say_debug("%s(): error while parsing "
					  "create statement for trigger",
					  __func__);
				return;
			}

			Hash *trigHash = &pSchema->trigHash;
			pTrigger = (Trigger*)sqlite3HashFind(
				trigHash, trig_name);
			pTrigger->tid = mp_decode_uint(&tid_opt);
			sqlite3_finalize(sqlite3_stmt);
		}

		delete[] trig_name;
		delete[] crt_stmt;
	} while (trigger_iterator.InProcess());
}

/**
 * Check if a tarantool space and an index numbers are encoded in the
 * sqlite page number.
 */
static char
check_num_on_tarantool_id(sql_tarantool_state * /*state*/, uint32_t num)
{
	uint32_t buf;
	buf = (num << 23) >> 23;
	if (buf) return 0;
	return !!(num & (1 << 30));
}

static void
state_add_index(sql_tarantool_state *state, SIndex *new_index)
{
	SIndex **new_indices = new SIndex*[state->cnt_indices + 1];
	memcpy(new_indices, state->indices,
	       state->cnt_indices * sizeof(SIndex *));
	new_indices[state->cnt_indices] = new_index;
	state->cnt_indices++;
	if (state->indices) delete[] state->indices;
	state->indices = new_indices;
}

static void
state_remove_index(sql_tarantool_state *state, SIndex *old_index)
{
	SIndex **new_indices = nullptr;
	int new_count = 0;
	for (int i = 0; i < state->cnt_indices; ++i) {
		if ((state->indices[i] != old_index) &&
			(state->indices[i]->tnum != old_index->tnum))
		{
			new_count++;
		}
	}
	new_indices = new SIndex*[new_count];
	for (int i = 0, j = 0; i < state->cnt_indices; ++i) {
		if ((state->indices[i] != old_index) &&
			(state->indices[i]->tnum != old_index->tnum))
		{
			new_indices[j] = state->indices[i];
			j++;
		}
	}
	state->cnt_indices = new_count;
	delete[] state->indices;
	state->indices = new_indices;
}

/**
 * Remove index from state->indices array
 */
static void
xsql_index_free(sqlite3 *db, SIndex *index)
{
	state_remove_index(db->tarantool.state, index);
}

/**
 * Clear tarantool space with given sqlite id.
 */
static void
xsql_space_truncate(sqlite3 *db, int root_page)
{
	(void)db;
	box_truncate(get_space_id_from(root_page));
}

/* T A R A N T O O L   C U R S O R   A P I */

/**
 * Constructor for TarantoolCursor inside pCur. Cursor will be
 * opened on index specified in iTable.
 *
 * @param state_ Pointer to sql_tarantool_state object.
 * @param iTable Sqlite3 root page number for opening cursor,
 *               but in tarantool it is used for containing
 *               index and space id.
 * @param pCur Sqlite3 cursor that will send all operations
 *               to its TarantoolCursor.
 * return SQLITE_OK if success.
 */
static int
xsql_cursor_create(Btree *p, int iTable, int wrFlag,
		  struct KeyInfo *pKeyInfo, BtCursor *pCur)
{
	sql_tarantool_state *state = p->db->tarantool.state;

	for (int i = 0; i < state->cnt_cursors; ++i) {
		if (state->cursors[i]->brother == pCur) {
			say_debug("%s(): trying to reinit "
				  "existing cursor\n", __func__);
			return SQLITE_ERROR;
		}
	}
	uint32_t num = (uint32_t)iTable;
	TrntlCursor *c = new TrntlCursor();
	c->key = new char[2];
	char *key_end = mp_encode_array(c->key, 0);
	int index_id = 0;
	int type = ITER_ALL;
	int space_id = get_space_id_from(num);
	index_id = get_index_id_from(num) % 15;
	uint32_t tnum = make_index_id(space_id, index_id);
	SIndex *xsql_index = NULL;
	for (int i = 0; i < state->cnt_indices; ++i) {
		if ((uint32_t)state->indices[i]->tnum == tnum) {
			xsql_index = state->indices[i];
			break;
		}
	}
	if (xsql_index == NULL) {
		say_debug("%s(): xsql_index not found, "
			  "space_id = %d, index_id = %d\n",
			  __func__, space_id, index_id);
		delete c;
		delete[] c->key;
		return SQLITE_ERROR;
	}
	c->cursor = TarantoolCursor(p->db, space_id, index_id,
				    type, c->key, key_end,
				    xsql_index, wrFlag, pCur);
	c->brother = pCur;
	pCur->trntl_cursor = (void *)c;
	pCur->pBtree = p;
	pCur->pBt = p->pBt;
	pCur->pgnoRoot = iTable;
	pCur->iPage = -1;
	pCur->curFlags = wrFlag;
	pCur->pKeyInfo = pKeyInfo;
	pCur->eState = CURSOR_VALID;
	if (state->cnt_cursors == 0) {
		state->cursors = new TrntlCursor*[1];
	} else {
		TrntlCursor **tmp = new TrntlCursor*[state->cnt_cursors + 1];
		memcpy(tmp, state->cursors, sizeof(TrntlCursor *) * state->cnt_cursors);
		delete[] state->cursors;
		state->cursors = tmp;
	}
	state->cursors[state->cnt_cursors++] = c;
	return SQLITE_OK;
}

/**
 * Move TarantoolCursor in pCur on first record in index.
 *
 * @param pRes Set pRes to 1 if space is empty.
 * return SQLITE_OK if success.
 */
static int
xsql_cursor_first(BtCursor *pCur, int *pRes)
{
	TrntlCursor *c = (TrntlCursor *)(pCur->trntl_cursor);
	return c->cursor.MoveToFirst(pRes);
}

/**
 * Move TarantoolCursor in pCur on last record in index.
 *
 * @param pRes Set pRes to 1 if space is empty.
 * return SQLITE_OK if success.
 */
static int
xsql_cursor_last(BtCursor *pCur, int *pRes)
{
	TrntlCursor *c = (TrntlCursor *)(pCur->trntl_cursor);
	return c->cursor.MoveToLast(pRes);
}

/**
 * Size of data in current record in bytes.
 *
 * @param pSize In that parameter actual size will be saved.
 * returns always SQLITE_OK
 */
static int
xsql_cursor_data_size(BtCursor *pCur, uint32_t *pSize)
{
	TrntlCursor *c = (TrntlCursor *)(pCur->trntl_cursor);
	return c->cursor.DataSize(pSize);
}

/**
 * Get data of current record in sqlite3 btree cell format.
 *
 * @param pAmt Actual size of data will be saved here.
 * returns pointer to record in sqlite btree cell format.
 */
static const void *
xsql_cursor_data_fetch(BtCursor *pCur, uint32_t *pAmt)
{
	TrntlCursor *c = (TrntlCursor *)(pCur->trntl_cursor);
	return c->cursor.DataFetch(pAmt);
}

/**
 * Same as trntl_cursor_data_size - for compatibility with
 * sqlite.
 */
static int
xsql_cursor_key_size(BtCursor *pCur, i64 *pSize)
{
	TrntlCursor *c = (TrntlCursor *)(pCur->trntl_cursor);
	return c->cursor.KeySize(pSize);
}

/**
 * Same as trntl_cursor_data_fetch - for compatibility with
 * sqlite.
 */
static const void *
xsql_cursor_key_fetch(BtCursor *pCur, uint32_t *pAmt)
{
	TrntlCursor *c = (TrntlCursor *)(pCur->trntl_cursor);
	return c->cursor.KeyFetch(pAmt);
}

/**
 * Move TarantoolCursor in pCur on next record in index.
 *
 * @param pRes This will be set to 0 if success and 1 if current record
 *             already is last in index.
 * returns SQLITE_OK if success
 */
static int
xsql_cursor_next(BtCursor *pCur, int *pRes)
{
	TrntlCursor *c = (TrntlCursor *)(pCur->trntl_cursor);
	return c->cursor.Next(pRes);
}

/**
 * Move TarantoolCursor in pCur on previous record in index.
 *
 * @param pRes This will be set to 0 if success and 1 if current record
 *             already is first in index.
 * returns SQLITE_OK if success
 */
static int
xsql_cursor_prev(BtCursor *pCur, int *pRes)
{
	TrntlCursor *c = (TrntlCursor *)(pCur->trntl_cursor);
	return c->cursor.Previous(pRes);
}

/**
 * Insert data in pKey into space on index of which is pointed Tarantool
 * Cursor in pCur.
 *
 * @param pKey Date in sqlite btree cell format that must be inserted.
 * @param nKey Size of pKey.
 * @param pData Data for inserting directly in table - not used for tarantool.
 * @param nData Size of pData.
 * Other params is not used now.
 */
static int
xsql_cursor_insert(BtCursor *pCur, const void *pKey,
		  i64 nKey, const void *pData, int nData, int nZero,
		  int appendBias, int seekResult)
{
	TrntlCursor *c = (TrntlCursor *)(pCur->trntl_cursor);
	return c->cursor.Insert(pKey, nKey, pData, nData, nZero, appendBias,
				seekResult);
}

/**
 * Delete tuple pointed by pCur.
 * @param bPreserve If this parameter is zero, then the cursor is left
 *                  pointing at an arbitrary location after the delete.
 *                  If it is non-zero, then the cursor is left in a state
 *                  such that the next call to Next() or Prev()
 *                  moves it to the same row as it would if the call to
 *                  DeleteCurrent() had been omitted.
 */
static int
xsql_cursor_delete_current(BtCursor *pCur, int bPreserve)
{
	(void)bPreserve;
	TrntlCursor *c = (TrntlCursor *)(pCur->trntl_cursor);
	return c->cursor.DeleteCurrent();
}

/**
 * Destructor for TarantoolCursor in pCur. Also removes
 * this cursor from sql_tarantool_state.
 */
static int
xsql_cursor_close(BtCursor *cursor)
{
	sqlite3 *db = cursor->pBt->db;
	sql_tarantool_state *state = db->tarantool.state;
	TrntlCursor *c = (TrntlCursor *)cursor->trntl_cursor;

	delete[] c->key;
	TrntlCursor **new_cursors = new TrntlCursor*[state->cnt_cursors - 1];
	for (int i = 0, j = 0; i < state->cnt_cursors; ++i) {
		if (state->cursors[i]->brother != cursor) {
			new_cursors[j++] = state->cursors[i];
		}
	}
	delete[] state->cursors;
	state->cnt_cursors--;
	if (state->cnt_cursors)
		state->cursors = new_cursors;
	else {
		state->cursors = nullptr;
		delete[] new_cursors;
	}
	delete c;
	sqlite3DbFree(db, cursor->pKey);
	cursor->pKey = 0;
	cursor->eState = CURSOR_INVALID;

	return SQLITE_OK;
}

/**
 * Count of tuples in index on that pCur is pointing.
 */
static int
xsql_cursor_count(BtCursor *pCur, i64 *pnEntry)
{
	TrntlCursor *c = (TrntlCursor *)(pCur->trntl_cursor);
	return c->cursor.Count(pnEntry);
}

/**
 * Move TarantoolCursor in pCur to first record that <= than pIdxKey -
 * unpacked sqlite btree cell with some data.
 *
 * @param pIdxKey Structure that contains data in sqlite btree cell
 *                format and to that index must be moved.
 * @param intKey Contains key if it is integer.
 * @param pRes Here results will be stored. If *pRes < 0 then
 *             current record either is smaller than pIdxKey/intKey or
 *             index is empty. If *pRes == 0 then pIdxKey/intKey equal
 *             to current record. If *pRes > 0 then current record is
 *             bigger than pIdxKey/intKey.
 */
static int
xsql_cursor_move_to_unpacked(BtCursor *pCur,
			    UnpackedRecord *pIdxKey, i64 intKey,
			    int /*biasRight*/, int *pRes,
			    RecordCompare xRecordCompare)
{
	TrntlCursor *c = (TrntlCursor *)pCur->trntl_cursor;
	return c->cursor.MoveToUnpacked(pIdxKey, intKey, pRes, xRecordCompare);
}

/**
 * Drop table or index with id coded in iTable.
 * Set *piMoved in zero for sqlite compatibility.
 */
static int
xsql_table_drop(Btree *p, int iTable, int *piMoved)
{
	int space_id = get_space_id_from(iTable);
	int index_id = get_index_id_from(iTable);
	int rc;
	char key[128], *key_end;
	*piMoved = 0;
	(void)p;
	say_debug("%s(): space_id: %d, index_id: %d",
		  __func__, space_id, index_id);
	if (index_id == 15) {
		rc = drop_all_indices(space_id);
		if (rc) {
			return SQLITE_ERROR;
		}
		//drop space
		key_end = mp_encode_array(key, 1);
		key_end = mp_encode_uint(key_end, space_id);
		rc = box_delete(BOX_SPACE_ID, 0, key, key_end, NULL);
	} else {
		//drop index
		key_end = mp_encode_array(key, 2);
		key_end = mp_encode_uint(key_end, space_id);
		key_end = mp_encode_uint(key_end, index_id);
		rc = box_delete(BOX_INDEX_ID, 0, key, key_end, NULL);
	}
	if (rc) {
		say_debug("%s(): error while droping = %s",
			__func__, box_error_message(box_error_last()));
		return SQLITE_ERROR;
	}
	return SQLITE_OK;
}

/**
 * Remove given trigger from _trigger.
 */
static void
xsql_trigger_drop(sqlite3 *db, Trigger *pTrigger)
{
	char key[10];
	(void)db;
	char *it = key;
	Table *pTab = (Table *)sqlite3HashFind(&pTrigger->pTabSchema->tblHash,
					       pTrigger->table);
	uint32_t id = get_space_id_from(pTab->tnum);
	it = mp_encode_array(it, 2);
	it = mp_encode_uint(it, id);
	it = mp_encode_uint(it, pTrigger->tid);

	box_delete(BOX_TRIGGER_ID, 0, key, it, NULL);
}


/* T A R A N T O O L   N E S T E D   F U N C S */

/**
 * Function for inserting into space.
 * sql_tarantool_state = argv[0], char *name = argv[1], struct Table = argv[2].
 */
static int
trntl_nested_insert_into_space(int argc, void *argv_)
{
	(void)argc;
	void **argv = (void **)argv_;
	sqlite3 *db = (sqlite3 *)argv[0];
	int rc;
	char *name = (char *)(argv[1]);
	if (!strcmp(name, "_space")) {
		Table *table = (Table *)argv[2];
		rc = insert_new_table_as_space(db, table, (char *)argv[3]);
		if (rc) {
			say_debug("%s(): error while inserting "
				  "new table as space", __func__);
			return SQLITE_ERROR;
		}
	} else if (!strcmp(name, "_index")) {
		SIndex *index = (SIndex *)argv[2];
		rc = insert_new_sindex_as_index(index);
		if (rc) {
			say_debug("%s(): error while insering "
				  "new sindex as index", __func__);
			return SQLITE_ERROR;
		}
	} else if (!strcmp(name, "_view")) {
		Table *table = (Table *)argv[2];
		char *crt_stmt = (char *)argv[3];
		rc = insert_new_view_as_space(db, table, crt_stmt);
		if (rc) {
			say_debug("%s(): error while inserting "
				  "new view as space", __func__);
			return SQLITE_ERROR;
		}
	} else if (!strcmp(name, "_trigger")) {
		Trigger *trigger = (Trigger *)argv[2];
		char *crt_stmt = (char *)argv[3];
		rc = insert_trigger(db, trigger, crt_stmt);
		if (rc) {
			say_debug("%s(): error while inserting "
				  "new trigger into _trigger\n",
				  __func__);
			return SQLITE_ERROR;
		}
	}
	say_debug("%s(): inserted into space %s",
		  __func__, (char *)(argv[1]));
	return SQLITE_OK;
}

/*  T A R A N T O O L    S T O R A G E   I N T E R F A C E    F O R
 *  S Q L I T E  */
extern "C" sql_tarantool_api tntxsql_api;

sql_tarantool_api tntsql_api = {
	.size = 0, /* not initialised yet */
	.state = NULL,
	.init = xsql_init,
	.finalize = xsql_finalize,
	/* meta */
	.table_drop = xsql_table_drop,
	.index_free = xsql_index_free,
	.trigger_drop = xsql_trigger_drop,
	.check_num_on_tarantool_id = check_num_on_tarantool_id,
	.trntl_nested_insert_into_space = trntl_nested_insert_into_space,
	.space_truncate = xsql_space_truncate,
	/* enumerate */
	.get_spaces = xsql_get_spaces,
	.get_triggers = xsql_get_triggers,
	/* cursor */
	.cursor_create = xsql_cursor_create,
	.cursor_close = xsql_cursor_close,
	.cursor_first = xsql_cursor_first,
	.cursor_last = xsql_cursor_last,
	.cursor_next = xsql_cursor_next,
	.cursor_prev = xsql_cursor_prev,
	.cursor_count = xsql_cursor_count,
	.cursor_data_size = xsql_cursor_data_size,
	.cursor_data_fetch = xsql_cursor_data_fetch,
	.cursor_key_size = xsql_cursor_key_size,
	.cursor_key_fetch = xsql_cursor_key_fetch,
	.cursor_move_to_unpacked = xsql_cursor_move_to_unpacked,
	.cursor_insert = xsql_cursor_insert,
	.cursor_delete_current = xsql_cursor_delete_current
};

/*  P U B L I C  */

static sqlite3 *db = NULL;

extern "C"
void
sql_init()
{
	int rc;
	/* assume database was already initialized */
	tntsql_api.size = sizeof(tntsql_api);
	rc = sqlite3_open("", &db);
	if (rc == SQLITE_OK) {
		assert(db);
	} else {
        /* XXX */
	}
}

extern "C"
void
sql_free()
{
	sqlite3_close(db); db = NULL;
}

extern "C"
sqlite3 *
sql_get() { return db; }
