#!/usr/bin/env tarantool
-- Simple Lint: check for unintended globals
--
-- One can spot all globals by checking the bytecode:
-- search for GSET and GGET instructions. Based on jit/bc.lua

local function is_main()
    return debug.getinfo(2).what == "main" and pcall(debug.getlocal, 5, 1) == false
end

local allow_globals = {
    -- modules
    coroutine    = true, debug          = true, io           = true,
    jit          = true, math           = true, os           = true,
    package      = true, string         = true, table        = true,
    bit          = true,
    -- variables
    _G           = true, _VERSION       = true, arg          = true,
    -- functions
    assert       = true, collectgarbage = true, dofile       = true,
    error        = true, getfenv        = true, getmetatable = true,
    ipairs       = true, help           = true, load         = true,
    loadfile     = true, loadstring     = true, module       = true,
    next         = true, pairs          = true, pcall        = true,
    print        = true, rawget         = true, rawset       = true,
    require      = true, select         = true, setfenv      = true,
    setmetatable = true, tonumber       = true, tostring     = true,
    type         = true, unpack         = true, xpcall       = true,
    -- tarantool
    box          = true, _TARANTOOL     = true, dostring     = true,
    tutorial     = true,
}

local jutil = require("jit.util")
local funcinfo, funcbc, funck = jutil.funcinfo, jutil.funcbc, jutil.funck
local band, shr = bit.band, bit.rshift
local sub, format = string.sub, string.format
local fun = require('fun')

-- GGET and GSET opcodes, numeric values different in v2.0 and v2.1
local function gget_gset() foo = bar end
local GGET = band(funcbc(gget_gset, 1), 0xff)
local GSET = band(funcbc(gget_gset, 2), 0xff)

local function print_global(func, pc, op, gkey)
    local fi = funcinfo(func, pc)
    local li
    if sub(fi.source, 1, 1) == '@' then
        li = sub(fi.source, 2)
    else
        li = format('[string "%s"]', fi.source)
    end
    li = li .. format(':%d', fi.currentline)
    return format('%s: Suspicious use of a global variable: %s', li, gkey)
end

local printers = {
    global = print_global
}
-- depending heavily on jit.util:
--  * funcinfo(fn) - self-explanatory;
--  * funck(fn, i) - get an item from constants table;
--                   i >= 0 integer constants,
--                   i <  0 object constants, including strings and
--                          prototypes for nested functions;
--  * funcbc(fn, i) - i-th bytecode instruction.
local function validate(func)
    local fi = funcinfo(func)
    local errors = {}
    if fi.children then
        for n = -1, -1000000000, -1 do
            local k = funck(func, n)
            if not k then
                break
            end
            if type(k) == "proto" then
                table.insert(errors, validate(k))
            end
        end
    end
    -- unpack results into one-layer array
    errors = fun.chain(unpack(errors)):totable()
    for pc = 1, 1000000000 do
        local ins = funcbc(func, pc)
        if not ins then
            break
        end
        local op = band(ins, 0xff)
        if op == GGET or op == GSET then
            local gkey = funck(func, -shr(ins, 16) - 1)
            if not allow_globals[gkey] then
                table.insert(errors, {'global', {func, pc, op, gkey}})
                -- check(func, pc, op, gkey)
             end
        end
    end
    -- must be error, since can't reach max PC
    return errors
end

if is_main() then
    if arg[1] == nil then
        error('Usage: lint.lua [-] [<script> ...]')
    end
    -- iterate over all given files
    local errors = fun.iter(arg):map(function(file_name)
        -- if we have '-' in the args, then try to read from stdio
        if file_name == '-' then
            file_name = nil
        end
        local code, err = loadfile(file_name)
        if code == nil then
            print(format('File loading failed - %s', err))
            return {}
        end
        return validate(code)
    end):totable()
    -- unpack results into one-layer array
    errors = fun.chain(unpack(errors)):totable()
    -- print results to stdio
    fun.iter(errors):each(function(err)
        local errtype, errargs = unpack(err)
        -- call custom printer function with custom arguments
        print(printers[errtype](unpack(errargs)))
    end)
    print(format("Total errors: %d", #errors))
    os.exit(#errors > 0 and 1 or 0)
else
    return {
        validate = validate,
        printers = printers
    }
end
