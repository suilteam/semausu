--
-- Created by IntelliJ IDEA.
-- User: carter
-- Date: 2020-04-02
-- Time: 10:15 a.m.
-- @module GatewayVerify fixture will test verifying a registered user vi route
-- POST /users/verify
--

local Gateway = require("scripts/gateway") { }
local Http,_,V = import("sys/http")

local GtyUsersVerify = Fixture('GatewayUsersVerify', "Tests verifying registered users")

GtyUsersVerify:before(function(ctx)
    -- ensure that the server is running prior to running test
    if ctx.gty == nil or not Gateway:running() or ctx.attrs.reset then
        ctx.gty = Gateway:restart(Swept.Data.GtyBin, Swept.Data.GtyConfig, ctx.attrs.reset)
        Test(Gateway:init(ctx), 'Gateway must be successfully initialized before continuing test')
    end
end)

GtyUsersVerify('VerifyUsersTest', "Tests different scenarios of verifying registered users")
:run(function(ctx)
    local users = Gateway.Data.Users1
    -- Unregistered user and invalid token verification
    local dummyToken = 'b4d819de-74f1-11ea-80ec-0242ac130004'
    local resp = Http(ctx.gty('/users/verify'), {
        method = 'POST',
        params = {email = users[1].Email, id = dummyToken}
    })
    V(resp):IsStatus(Http.BadRequest, "Verification should fail since token and email are invalid")

    -- register users
    local tokens = {}
    for _,user in ipairs(users) do
        resp = Http(ctx.gty('/users/register'), {
            method = 'POST',
            form = user
        })
        V(resp):IsStatus(Http.Created, "Registering a valid user '%s' should be successful", user.Email)
        -- returning token in the body of a request is not a testable feature of the gateway server
        -- it is only build for swept, in production cases an email will be sent to the user to verify
        tokens[#tokens+1] = resp.body
    end

    -- attempt verify user with invalid token (not assigned to any user)
    resp = Http(ctx.gty('/users/verify'), {
        method = 'POST',
        params = {email = users[1].Email, id = dummyToken}
    })
    V(resp):IsStatus(Http.BadRequest, 'Verifying registration with invalid token should be rejected')

    -- attempt verifying users with a token belonging to another user
    resp = Http(ctx.gty('/users/verify'), {
        method = 'POST',
        params = {email = users[1].Email, id = tokens[2]}
    })
    V(resp):IsStatus(Http.BadRequest, 'Verifying registration token not assigned to user should be rejected')

    -- verify with valid tokens
    for i=1,#tokens do
        resp = Http(ctx.gty('/users/verify'), {
            method = 'POST',
            params = {email = users[i].Email, id = tokens[i]}
        })
        V(resp):IsStatus(Http.Ok, 'Verifying registration token with valid email and token should succeed')
    end
end)
:attrs({reset = true}) -- reset server only on first test

return GtyUsersVerify