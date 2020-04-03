--
-- Created by IntelliJ IDEA.
-- User: carter
-- Date: 2020-04-02
-- Time: 11:37 a.m.
--
-- @module GatewayUssersLogin fixture tests logging into gateway at route
-- POST '/users/login'
--

local Gateway = require("scripts/gateway") { }
local Http,_,V,Jwt = import("sys/http")

local GtyUsersLogin = Fixture('GatewayUsersLogin', "Tests the POST '/users/login' routes")

GtyUsersLogin:before(function(ctx)
    -- ensure that the server is running prior to running test
    if ctx.gty == nil or not Gateway:running() or ctx.attrs.reset then
        ctx.gty = Gateway:restart(Swept.Data.GtyBin, Swept.Data.GtyConfig, ctx.attrs.reset)
        Test(Gateway:init(ctx), 'Gateway must be successfully initialized before continuing test')
        -- this requires that users be  registered onto the servers
        for _,user in ipairs(Gateway.Data.Users1) do
            -- register user
            local resp = Http(ctx.gty('/users/register'), {
                method = 'POST',
                form   = user
            })
            V(resp):IsStatus(Http.Created, "User '%s' should successfully be registered", user.Email)
            local token = resp.body
            -- verify user
            resp = Http(ctx.gty('/users/verify'), {
                method = 'POST',
                params = {email = user.Email, id = token}
            })
            V(resp):IsStatus(Http.Ok, "User '%s' should be successfully verified to continue test", user.Email)
        end
        -- the following user is registered but not verified
        local resp = Http(ctx.gty('/users/register'), {
            method = 'POST',
            form   = Gateway.Data.Users2[1]
        })
        V(resp):IsStatus(Http.Created, "User '%s' should successfully be registered", user.Email)
    end
end)

GtyUsersLogin('UsersLoginInvalidParams', 'Test logging into server with invalid parameters')
:run(function(ctx)
    local verified = Gateway.Data.Users1
    local other = Gateway.Data.Users2

    local data = {
        {
            form = {Email = verified[1].Email}, -- password missing
            Expect = 'MissingFields'
        }, {
            form = {Passwd = verified[1].Passwd}, -- email missing,
            Expect = 'MissingFields'
        }, {
            form = {Email = other[1].Email, Passwd = other[1].Passwd}, -- unregistered user
            Expect = 'UserNotVerified'
        }, {
            form = {Email = other[2].Email, Passwd = other[2].Passwd}, -- un-verified user
            Expect = 'UserNotRegistered'
        }, {
            form = {Email = verified[1].Email, Passwd = verified[2].Passwd}, -- invalid password provided
            Expect = 'InvalidPassword'
        }, {
            form = {Email = verified[1].Email, Passwd = verified[1].Passwd..'a' }, -- invalid password provided
            Expect = 'InvalidPassword'
        }, {
            form = {Email = verified[1].Email, Passwd = verified[1].Passwd:sub(2) }, -- invalid password provided
            Expect = 'InvalidPassword'
        }

        -- @TODO test UserPasswordExpired and UserBlocked
    }

    for i,test in ipairs(data) do
        local resp = Http(ctx.gty('/users/login'), {
            method = 'POST',
            form = test.form
        })
        V(resp):IsStatus(Http.Forbidden,
            "Request to login user with invalid arguments %d:{Email %s, Passwd: %s} should be denied",
            i, tostring(test.form.Email), tostring(test.form.Passwd))
        local json = resp:json()
        Test(json.status, test.Expect, "Login with invalid parameters must return '%s' status", form.Expect)
        Test(not resp.headers.Authorization, 'Authorization token must not be issued on loggin failure')
    end
end)
:attrs({reset =  true})

GtyUsersLogin('UsersLoginValidCredentials', 'Verify that logging in with valid credentials grants a token')
:run(function(ctx)
    local valid = Gateway.Data.Users1
    for _,user in ipairs(valid) do
        local resp = Http(ctx.gty('/users/login'), {
            method = 'POST',
            form = {Email = user.Email, Passwd = user.Passwd}
        })
        V(resp):IsStatus(Http.Ok, "Logging a registered and verified user '%s' must succeed", user.Email)
        local token = resp.headers.Authorization
        local jwt = Jwt(token)
        Test(jwt ~= nil, "Returned token should be a valid JWT")
        Equal(jwt('aud'), user.Email, "The returned token must be addressed to the current used")

        -- loging in an already logged in user will give back the same token
        -- only when the user provides valid login credentials
        resp = Http(ctx.gty('/users/login'), {
            method = 'POST',
            form = {Email = user.Email, Passwd = user.Passwd}
        })
        V(resp):IsStatus(Http.Ok, "Logging a registered and verified user '%s' must succeed", user.Email)
        Equal(token, resp.headers.Authorization, 'Login in second time should return  same token')

        -- log in with invalid credentials
        resp = Http(ctx.gty('/users/login'), {
            method = 'POST',
            form = {Email = user.Email, Passwd = 'invalid@pass'}
        })
        V(resp):IsStatus(Http.Forbidden, "Login second time with invalid credentions must be denied")
    end
end)

return GtyUsersLogin