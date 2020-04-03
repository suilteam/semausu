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

local User = setmetatable({}, {
    __call = function(this, email, fn, ln, passwd, attrs)
        local user = attrs or {}
        user.Email = email
        user.FirstName = fn
        user.LastName = ln
        user.Passwd = passwd
        return user
    end
})

---
--- Represents data that can used in test cases
---
Gateway.Data = {
    Admin = User('admin@suilteam.com','Admin','Gateway', 'admin123'),
    ---
    --- @field ValidUsers table Is list of valid users that can be used for testing
    ---
    Users1 = {
        User('user1@suilteam.com', 'User1', 'Testing', 'user1Pass'),
        User('user2@suilteam.com', 'User2', 'Testing', 'user2Pass'),
        User('user3@suilteam.com', 'User3', 'Testing', 'user3Pass')
    },
    Users2 = {
        User('user4@suilteam.com', 'User4', 'Testing', 'user4Pass'),
        User('user5@suilteam.com', 'User5', 'Testing', 'user5Pass')
    }
}
Gateway._endpointApi = function(this, name, handler)
    assert(name and type(name) == 'string', 'The name of the API must be a string')
    assert(handler and type(handler) == 'function', 'handler must be an executable function')
    this[name] = handler
    this[name..'0'] = function(self, ctx, ...)
        Test(self:running(), "%s: Gateway server must be running before invoking target API", name)
        return self[name](self, ctx, ...)
    end
end

Gateway:_endpointApi('init', function(this, ctx)
    local resp = Http(ctx.gty('/app-init'), {
        method = 'POST',
        body = { Administrator = this.Data.Admin }
    })
    return resp.status == Http.Ok
end)

Gateway:_endpointApi('register', function(this, ctx, user)
    local resp = Http(ctx.gty('/users/register'), {
        method = 'POST',
        form   = user
    })
    if resp.status ~= Http.Created then
        local json = resp:json()
        return false, {"Registering user %s failed: %s", user.Email, json.status}
    end
    -- verify user
    resp = Http(ctx.gty('/users/verify'), {
        method = 'POST',
        params = {email = user.Email, id = resp.body}
    })
    if resp.status ~= Http.Ok then
        local json = resp:json()
        return false, {"Registering user %s failed: %s", user.Email, json.status}
    end
    return true, {'User sucessfully logged in'}
end)

Gateway:_endpointApi('login', function(this, ctx, user)
    local resp = Http(ctx.gty('/users/login'), {
        method = 'POST',
        form = {Email = user.Email, Passwd = user.Passwd}
    })
    if resp.status == Http.Ok then return resp.headers.Authorization, {'User successfully logged in'} end
    -- failed to login user
    local json = resp:json()
    return false, {"Logging in user '%s' failed: %s", user.Email, json.status}
end)

return Gateway