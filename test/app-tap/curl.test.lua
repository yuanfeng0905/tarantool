#!/usr/bin/env tarantool

local tap = require('tap')
local curl  = require('curl')
local json  = require('json')
local test = tap.test("curl")
local log = require('log')
-- Supress console log messages
log.level(4)

-- create ev_loop
box.cfg{logger='tarantool.log'}


test:plan(3)
test:test("basic http post/get", function(test)
    test:plan(11)

    local http = curl.http({pool_size=1})

    test:ok(http ~= nil, "client is created")
    local url = "http://httpbin.org/"
    local headers   = { my_header = "1", my_header2 = "2" }
    local my_body   = { key = "value" }
    local json_body = json.encode(my_body)

    local r = http:get(url .. "get" ,{headers = headers})
    test:is(r.code, 200, "http code page exists")
    test:isnt(r.body:len(), 0,"not empty body page exists")

    r = http:get(url .. 'this/page/not/exists',
                       {headers = headers} )
    test:is(r.code, 404, "http_code page not exists")
    test:isnt(r.body:len(), 0, " not empty body page not exists")

    r = http:get('https://tarantool.org/', {headers = headers})
    test:is(r.code, 200, "https check: http code page exists")
    test:isnt(r.body:len(), 0,"https check: not empty body page exists")

    local r = http:post('http://httpbin.org/post', json_body,
                        {headers = headers,
                         keepalive_idle = 30,
                         keepalive_interval = 60,})

    local obody = json.decode(r.body)
    test:ok( r.code == 200 and
            obody.headers['My-Header'] == headers.my_header and 
            obody.headers['My-Header2'] == headers.my_header2,
            "post headers")
    
    local r = http:put('http://httpbin.org/put', json_body,
                        {headers = headers,
                         keepalive_idle = 30,
                         keepalive_interval = 60,})
     
    obody = json.decode(r.body)
    test:ok( r.code == 200 and
            obody.headers['My-Header'] == headers.my_header and 
            obody.headers['My-Header2'] == headers.my_header2,
            "put headers")
    

    local st = http:stat()
    local pst = http:pool_stat()
    test:ok(st.sockets_added == st.sockets_deleted and
            st.active_requests == 0 and
            pst.pool_size == 1 and
            pst.free == pst.pool_size, "stats checking")
    -- issue https://github.com/tarantool/curl/issues/3        
    local data = {a = 'b'}
    local headers = {}
    headers['Content-Type'] = 'application/json'
    r = http:post('https://httpbin.org/post', json.encode(data),
                        {headers=headers})
    test:ok(r.code == 200 and json.decode(r.body)['json']['a'] == data['a'], "tarantool/curl/issues/3")

    http:free()
end)

test:test("special methods", function(test)
    test:plan(4)
    local http = curl.http({pool_size=1})
    local url = "https://httpbin.org/"

    local delete_data = http:delete(url .. "delete")
    test:is(delete_data.code, 200, "HTTP:DELETE request")

    local options_data = http:http_options(url)
    test:is(options_data.code, 200, "HTTP:OPTIONS request")
    

    local head_data = http:head(url)
    local some_header = "Server: nginx"
    test:is(head_data.code, 200, "HTTP:HEAD request code")
    test:ok(string.find(head_data.headers, some_header), "HTTP:HEAD request content")
    http:free()
end)



test:test("tests with server", function(test)
    test:plan(6)
    local fiber = require('fiber')
    

    os.execute("./server.js & echo $! > ./nd.pid")

    local http = curl.http()
    local host    = '127.0.0.1:10000'
    
    fiber.sleep(1)

    local trace_data = http:trace(host)
    test:is(trace_data.code, 200, "HTTP:TRACE request")
    
    
    local connect_data = http:http_connect(host)
    test:is(connect_data.code, 200, "HTTP:CONNECT request")
 
    http:free()
    

    local num     = 10
    local curls   = { }
    local headers = { }

    -- Init [[
    for i = 1, num do
      headers["My-header" .. i] = "my-value"
    end

    for i = 1, num do
      table.insert(curls, {url = host .. '/',
                           http = curl.http(),
                           body = json.encode({stat = box.stat(),
                                               info = box.info() }),
                           headers = headers,
                           connect_timeout = 5,
                           read_timeout = 5,
                           dns_cache_timeout = 1,
                          } )
    end
    -- ]]

    -- Start test
    for i = 1, num do

      local obj = curls[i]

      for j = 1, 10 do
          fiber.create(function()
              obj.http:post(obj.url, obj.body,
                                {headers = obj.headers,
                                 keepalive_idle = 30,
                                 keepalive_interval = 60,
                                 connect_timeout = obj.connect_timeout,
                                 read_timeout = obj.read_timeout,
                                 dns_cache_timeout = obj.dns_cache_timeout, })
          end )
          fiber.create(function()
              obj.http:get(obj.url,
                                {headers = obj.headers,
                                 keepalive_idle = 30,
                                 keepalive_interval = 60,
                                 connect_timeout = obj.connect_timeout,
                                 read_timeout = obj.read_timeout,
                                 dns_cache_timeout = obj.dns_cache_timeout, })
          end )
      end
    end
    local ok_sockets_added = true
    local ok_active = true
    local ok_poolsize = true
    local ok_timeout = true
    -- Join test
    fiber.create( function()
      local os   = require('os')
      local yaml = require('yaml')
      local rest = num

      local ticks = 0

      while true do

        fiber.sleep(1)

        for i = 1, num do

          local obj = curls[i]

          if obj.http ~= nil and obj.http:stat().active_requests == 0 then
            local st = obj.http:stat()
            if st.sockets_added ~= st.sockets_deleted then
                ok_sockets_added = false
                rest = 0
            end
            if st.active_requests ~= 0 then
                ok_active = false
                rest = 0
            end

            local pst = obj.http:pool_stat()
            if pst.pool_size ~= pst.free then
                ok_poolsize = false
                rest = 0
            end

            obj.http:free()
            rest = rest - 1
            curls[i].http = nil
          end

        end

        if rest <= 0 then
          break
        end

        ticks = ticks + 1

        -- Test failed
        if ticks > 80 then
            ok_timeout = false
            rest = 0
        end

      end
      os.exit(0)
    end)
    fiber.sleep(1)
    test:ok(ok_sockets_added, "Concurrent.Test free sockets")
    test:ok(ok_poolsize, "Concurrent.Test poolsize and free fibers")
    test:ok(ok_active, "Concurrent.Test no active requests")
    test:ok(ok_timeout, "Concurrent.Test timeout not expired")

    os.execute("cat ./nd.pid | xargs kill -s TERM")
end)

