#!/usr/bin/env tarantool

local tap = require('tap')
local curl  = require('curl')
local json  = require('json')

tap:test("basic http pos/get", function(test)
    test:plan(9)

    local http = curl.http({pool_size=1})

    test:ok(http ~= nil, "client is created")

    local headers   = { my_header = "1", my_header2 = "2" }
    local my_body   = { key="value" }
    local json_body = json.encode(my_body)

    local r = http:get('https://tarantool.org/this/page/not/exists',
                       {headers=headers} )
    test:is(r.code,404, "http_code page not exists")
    test:isnt(r.body:len(), 0, " not empty body page not exists")

    r = http:get('https://tarantool.org/', {headers=headers})
    test:is(r.code, 200, "http code page exists")
    test:isnt(r.body:len(), 0,"not empty body page exists")

    local r = http:post('http://httpbin.org/post', json_body,
                        {headers=headers,
                         keepalive_idle = 30,
                         keepalive_interval = 60,})
    local obody = json.decode(r.body)
    test:ok( r.code == 200 and
            obody.headers['My-Header'] == headers.my_header and 
            obody.headers['My-Header2'] == headers.my_header2,
            "post headers")
    
    local r = http:put('http://httpbin.org/put', json_body,
                        {headers=headers,
                         keepalive_idle = 30,
                         keepalive_interval = 60,})
     
    local obody = json.decode(r.body)
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
    data = {a = 'b'}
    headers = {}
    headers['Content-Type'] = 'application/json'
    r = http:post('https://httpbin.org/post', json.encode(data),
                        {headers=headers})
    test:is(r.code == 200 and json.decode(res.body)['json']['a'] == data['a'], "tarantool/curl/issues/3")

    http:free()
end)

tap:test("special methods", function(test)
    test.plan()


end)

tap:test("ev_loop test", function(test)
    local fiber = require('fiber')
    local json  = require('json')

    local num     = 10
    local host    = '127.0.0.1:10000'
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

    -- Join test
    fiber.create(function()

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
            assert(st.sockets_added == st.sockets_deleted)
            assert(st.active_requests == 0)

            local pst = obj.http:pool_stat()
            assert(pst.pool_size == pst.free)

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
            test:ok(false, "timeout expired") 
        end

      end

      os.exit(0)
    end )
end)
