#include "sql.h"
#include "box/sql.h"

#include "sqlite3.h"
#include "sqliteInt.h"
#include "lua/utils.h"
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

struct prep_stmt
{
	sqlite3_stmt *stmt;
};

struct prep_stmt_list
{
	uint8_t         *mem_end;   /* denotes actual size of sql_ctx struct */
	uint32_t         pool_size; /* mem at the end used for aux allocations;
				       pool grows from mem_end
				       towards stmt[] array */
	uint32_t         last_select_stmt_index; /* UINT32_MAX if no selects */
	uint32_t         column_count; /* in last select stmt */
	uint32_t         stmt_count;
	struct prep_stmt stmt[6];  /* overlayed with the mem pool;
				      actual size could be larger or smaller */
	/* uint8_t mem_pool[] */
};

/* Release resources and free the list itself, unless it was preallocated
 * (i.e. l points to an automatic variable) */
static void
prep_stmt_list_free(struct prep_stmt_list *l,
		    struct prep_stmt_list *prealloc)
{
	if (l == NULL)
		return;
	for (size_t i = 0, n = l->stmt_count; i < n; i++)
		sqlite3_finalize(l->stmt[i].stmt);
	if (l != prealloc)
		free(l);
}

static struct prep_stmt_list *
prep_stmt_list_init(struct prep_stmt_list *prealloc)
{
	prealloc->mem_end = (uint8_t *)(prealloc + 1);
	prealloc->pool_size = 0;
	prealloc->last_select_stmt_index = UINT32_MAX;
	prealloc->column_count = 0;
	prealloc->stmt_count = 0;
	return prealloc;
}

/* Allocate mem from the prep_stmt_list pool.
 * If not enough space is available, reallocates the list.
 * If reallocation is needed but l was preallocated, old mem is left
 * intact and a new memory chunk is allocated. */
static void *
prep_stmt_list_palloc(struct prep_stmt_list **pl,
		      size_t size, size_t alignment,
		      struct prep_stmt_list *prealloc)
{
	assert((alignment & (alignment - 1)) == 0); /* 2 ^ k */
	assert(alignment <= alignof(l->stmt[0]));

	struct prep_stmt_list *l = *pl;
	uint32_t pool_size = l->pool_size;
	uint32_t pool_size_max = (uint32_t)(
		l->mem_end - (uint8_t *)(l->stmt + l->stmt_count)
	);

	assert(UINT32_MAX - pool_size >= size);
	pool_size += size;

	assert(UINT32_MAX - pool_size >= alignment - 1);
	pool_size += alignment - 1;
	pool_size &= ~(alignment - 1);

	if (pool_size > pool_size_max) {
		size_t prev_size = l->mem_end - (uint8_t *)l;
		size_t size = prev_size;
		while (size < prev_size + (pool_size - pool_size_max)) {
			assert(SIZE_MAX - size >= size);
			size += size;
		}
		if (l == prealloc)
			l = malloc(size);
		else
			l = realloc(l, size);
		if (l == NULL)
			return NULL;
		*pl = l;
		l->mem_end = (uint8_t *)l + size;
		/* move the pool data */
		memmove((uint8_t *)l + prev_size - l->pool_size,
			l->mem_end - l->pool_size,
			l->pool_size);
	}

	l->pool_size = pool_size;
	return l->mem_end - pool_size;
}

/* push new stmt; reallocate memory if needed
 * returns a pointer to the new stmt or NULL if out of memory.
 * If reallocation is needed but l was preallocated, old mem is left
 * intact and a new memory chunk is allocated. */
static struct prep_stmt *
prep_stmt_list_push(struct prep_stmt_list **pl,
		    struct prep_stmt_list *prealloc)
{
	struct prep_stmt_list *l;
	/* make sure we don't collide with the pool */
	if (prep_stmt_list_palloc(pl, sizeof(l->stmt[0]), 1,
				  prealloc) == NULL)
		return NULL;
	l = *pl;
	l->pool_size -= sizeof(l->stmt[0]);
	return l->stmt + (l->stmt_count++);
}

/* Create prep_stmt_list from sql statement(s).
 * Memory for the resulting list may be malloc-ed
 * unless the result fits in prealloc-ed object. */
static int
prep_stmt_list_create(struct prep_stmt_list **pl,
		      struct sqlite3 *db,
		      const char *sql, size_t length,
		      struct prep_stmt_list *prealloc)
{
	int rc;
	assert(length <= INT_MAX);
	const char *sql_end = sql + length;
	struct prep_stmt_list *l = prep_stmt_list_init(prealloc);
	while (sql != sql_end) {
		struct prep_stmt *ps = prep_stmt_list_push(&l, prealloc);
		int column_count;
		if (ps == NULL) {
			rc = SQLITE_NOMEM;
			goto error;
		}
		rc = sqlite3_prepare_v2(db, sql, (int)(sql_end - sql),
					&ps->stmt, &sql);
		if (rc != SQLITE_OK)
			goto error;
		if (ps->stmt == NULL) {
			/* only whitespace */
			assert(sql == sql_end);
			l->stmt_count --;
			break;
		}
		column_count = sqlite3_column_count(ps->stmt);
		if (column_count != 0) {
			l->last_select_stmt_index = l->stmt_count - 1;
			l->column_count = column_count;
		}
	}
	/* reserve char[column_count] for a typestr */
	assert(l->pool_size == 0);
	if (prep_stmt_list_palloc(&l, l->column_count, 1, prealloc) == NULL) {
		rc = SQLITE_NOMEM;
		goto error;
	}
	*pl = l;
	return SQLITE_OK;
error:
	prep_stmt_list_free(l, prealloc);
	return rc;
}

static void
lua_push_column_names(struct lua_State *L, struct prep_stmt_list *l)
{
	sqlite3_stmt *stmt = l->stmt[l->last_select_stmt_index].stmt;
	int n = l->column_count;
	lua_createtable(L, n, 0);
	for (int i = 0; i < n; i++) {
		const char *name = sqlite3_column_name(stmt, i);
		lua_pushstring(L, name == NULL ? "" : name);
		lua_rawseti(L, -2, i+1);
	}
}

static void
lua_push_row(struct lua_State *L, struct prep_stmt_list *l)
{
	sqlite3_stmt *stmt = l->stmt[l->last_select_stmt_index].stmt;
	int column_count = l->column_count;
	char *typestr = (void *)(l->mem_end - column_count);

	lua_createtable(L, column_count, 0);
	lua_rawgeti(L, LUA_REGISTRYINDEX, luaL_array_metatable_ref);
	lua_setmetatable(L, -2);

	for (int i = 0; i < column_count; i++) {
		int type = sqlite3_column_type(stmt, i);
		switch (type) {
		case SQLITE_INTEGER:
			typestr[i] = 'i';
			lua_pushinteger(L, sqlite3_column_int(stmt, i));
			break;
		case SQLITE_FLOAT:
			typestr[i] = 'f';
			lua_pushnumber(L, sqlite3_column_double(stmt, i));
			break;
		case SQLITE_TEXT: {
			const void *text = sqlite3_column_text(stmt, i);
			typestr[i] = 's';
			lua_pushlstring(L, text,
					sqlite3_column_bytes(stmt, i));
			break;
		}
		case SQLITE_BLOB: {
			const void *blob = sqlite3_column_blob(stmt, i);
			typestr[i] = 'b';
			lua_pushlstring(L, blob,
					sqlite3_column_bytes(stmt, i));
			break;
		}
		case SQLITE_NULL:
			typestr[i] = '-';
			lua_rawgeti(L, LUA_REGISTRYINDEX, luaL_nil_ref);
			break;
		default:
			typestr[i] = '?';
			assert(0);
		}
		lua_rawseti(L, -2, i+1);
	}

	lua_pushlstring(L, typestr, column_count);
	lua_rawseti(L, -2, 0);
}

static int
lua_sql_execute(struct lua_State *L)
{
	int rc;
	sqlite3 *db = sql_get();
	struct prep_stmt_list *l = NULL, prealloc;
	size_t len;
	const char *sql;

	if (db == NULL)
		return luaL_error(L, "not ready");

	sql = lua_tolstring(L, 1, &len);
	if (sql == NULL)
		return luaL_error(L, "usage: box.sql.execute(sqlstring)");

	rc = prep_stmt_list_create(&l, db, sql, len, &prealloc);
	switch (rc) {
	case SQLITE_OK:
		break;
	case SQLITE_NOMEM:
		goto outofmem;
	default:
		goto sqlerror;
	}

	for (uint32_t i = 0, n = l->stmt_count; i < n; i++) {
		sqlite3_stmt *stmt = l->stmt[i].stmt;
		if (i == l->last_select_stmt_index) {

			/* create result table */
			lua_createtable(L, 7, 0);
			lua_pushvalue(L, lua_upvalueindex(1));
			lua_setmetatable(L, -2);
			lua_push_column_names(L, l);
			lua_rawseti(L, -2, 0);

			int row_count = 0;
			while ((rc = sqlite3_step(stmt) == SQLITE_ROW)) {
				lua_push_row(L, l);
				row_count++;
				lua_rawseti(L, -2, row_count);
			}

			if (rc != SQLITE_OK)
				goto sqlerror;
		} else {
			while ((rc = sqlite3_step(stmt) == SQLITE_ROW));
			if (rc != SQLITE_OK)
				goto sqlerror;
		}
	}
	rc = l->last_select_stmt_index != UINT32_MAX;
	prep_stmt_list_free(l, &prealloc);
	return rc;
sqlerror:
	prep_stmt_list_free(l, &prealloc);
	lua_pushstring(L, sqlite3_errmsg(db));
	return lua_error(L);
outofmem:
	prep_stmt_list_free(l, &prealloc);
	return luaL_error(L, "out of memory");
}

void
box_lua_sqlite_init(struct lua_State *L)
{
	static const struct luaL_reg module_funcs [] = {
		{"execute", lua_sql_execute},
		{NULL, NULL}
	};

	/* used by lua_sql_execute via upvalue */
	lua_createtable(L, 0, 1);
	lua_pushstring(L, "sequence");
	lua_setfield(L, -2, "__serialize");

	luaL_openlib(L, "box.sql", module_funcs, 1);
	lua_pop(L, 1);
}

