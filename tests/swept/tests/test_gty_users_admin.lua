--
-- Created by IntelliJ IDEA.
-- User: carter
-- Date: 2020-04-06
-- Time: 1:03 p.m.
--
-- @module GatewayUsersAdmin A fixture dedicated to testing user administration, e.g blocking users

local Gateway = require("scripts/gateway") { }
local Http,_,V,Jwt = import("sys/http")
local Json = import('sys/json')
local GatewayUsersAdmin = Fixture('GatewayUsersAdmin', "Tests user administration routes by administrator accounts")

GatewayUsersAdmin:before(function(ctx)
    -- ensure that the server is running prior to running test
    if ctx.gty == nil or not Gateway:running() or ctx.attrs.reset then
        ctx.gty = Gateway:restart(Swept.Data.GtyBin, Swept.Data.GtyConfig, ctx.attrs.reset)
        Test(Gateway:init(ctx), 'Gateway must be successfully initialized before continuing test')
        -- register a basic, admin user already registered with Gateway:init
        for _,user in ipairs(Gateway.Data.Users1) do
            Test(Gateway:register(ctx, user))
        end
        -- login admin and user1[1]
        local tok, msg = Gateway:login(ctx, Gateway.Data.Admin)
        Test(tok, table.unpack(msg))
        ctx.gty.tokens = {Admin = tok }
    end
end)

GatewayUsersAdmin("BlockingUnblockingUsers", "Tests blocking and unblocking user accounts")
:run(function(ctx)
    -- block a user using admin token
    local resp = Http(ctx.gty('/users/block'), {
        method = 'POST',
        headers = { Authorization = ctx.gty.tokens.Admin },
        params = { email = Gateway.Data.Users1[1].Email, reason = "Blocking user for testing purposes" }
    })
    V(resp):IsStatus(Http.Ok, "Blocking user by administrator must be successfull")

    -- blocking an already blocked user
    resp = Http(ctx.gty('/users/block'), {
        method = 'POST',
        headers = { Authorization = ctx.gty.tokens.Admin },
        params = { email = Gateway.Data.Users1[1].Email, reason = "Blocking user for testing purposes" }
    })
    V(resp):IsStatus(Http.BadRequest, "Blocking an already blocked user should fail")

    -- blocked user cannot login
    resp =  Http(ctx.gty('/users/login'), {
        method = 'POST',
        form  = {Email = Gateway.Data.Users1[1].Email, Passwd = Gateway.Data.Users1[1].Passwd}
    })
    V(resp):IsStatus(Http.Forbidden, "User must not be allowed to login when blocked")
    local json = resp:json()
    Equal(json.status, 'UserBlocked', "Response must note that user has been blocked")

    -- trying to block a user with a non-admin route is access denied
    local tok,msg = Gateway:login(ctx, Gateway.Data.Users1[2])
    Test(tok, table.unpack(msg))
    resp = Http(ctx.gty('/users/block'), {
        method = 'POST',
        headers = { Authorization = tok },
        params = { email = Gateway.Data.Users1[3].Email, reason = "Blocking user for testing purposes" }
    })
    V(resp):IsStatus(Http.Unauthorized, "Non-administrator accounts cannot block other accounts")
end)
:attrs({reset =  true})

return GatewayUsersAdmin





