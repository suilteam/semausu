---
--- @script Provides API's to interact with the test server
---         controlling the gateway endpoint
---

local Http,_,V = import("sys/http")

local Gateway = setmetatable({
    restart = function(this, binary, config, reset)
        config = config or Swept.Data.GtyConfig
        binary = binary or Swept.Data.GtyBin
        assert(config ~= nil, "Gateway configuration path required")
        assert(binary ~= nil, "Gateway binary binary required")
        Log:trc("restarting gateway {config: %s, binary: %s rest: %s}", config, binary, tostring(reset))

        local args = {"start", "-C", config}
        if reset then args[#args + 1] = "-r"; end

        local resp = Http(this.url..'/restart', {
            method = "GET",
            body = {
                bin = binary,
                args = args
            }
        })
        V(resp):IsStatus(Http.Ok, "Server should be launched successfully")
        Log:trc("Gateway server restarted %s", resp.body);

        return setmetatable(resp:json(), {
            __call = function(this, uri, ...)
                local args = {...}
                local path = this.server..uri;
                for _,v in ipairs(args) do
                    path = path..'/'..tostring(v)
                end
                return path
            end
        })
    end,
    running = function(this)
        local resp = Http(this.url..'/running', {
            method = "GET"
        })
        return resp.status == Http.Ok
    end,
    stop = function(this)
        Log:trc("stopping gateway servers")
        local resp = Http(this.url..'/stop', {
            method = "POST"
        })
        V(resp):IsStatus(Http.Ok, "Failed to stop running instance of server")
    end
}, {
    __call = function(this, attrs)
        attrs = attrs or {}
        return setmetatable({
            url = (attrs.server or Swept.Data.GtyServer)..':'..tostring(attrs.port or Swept.Data.GtyPort)
        }, {
            __index = this
        })
    end
})

return Gateway