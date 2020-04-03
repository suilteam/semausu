--
-- Created by IntelliJ IDEA.
-- User: carter
-- Date: 2020-04-02
-- Time: 3:01 p.m.
--
-- @module GatewayUserAccessToken The purpose of this fixture is to test
--  the usage of user access token

local Gateway = require("scripts/gateway") { }
local Http,_,V,Jwt = import("sys/http")
local Json = import('sys/json')
local GtyUserAccessToken = Fixture('GatewayUserAccessToken', "Tests the validity and usability of tokens granted when users login")

GtyUserAccessToken:before(function(ctx)
    -- ensure that the server is running prior to running test
    if ctx.gty == nil or not Gateway:running() or ctx.attrs.reset then
        ctx.gty = Gateway:restart(Swept.Data.GtyBin, Swept.Data.GtyConfig, ctx.attrs.reset)
        Test(Gateway:init(ctx), 'Gateway must be successfully initialized before continuing test')
        -- register a basic, admin user already registered with Gateway:init
        Test(Gateway:register(ctx, Gateway.Data.Users1[1]))
        -- login admin and user1[1]
        local tok, msg = Gateway:login(ctx, Gateway.Data.Admin)
        Test(tok, table.unpack(msg))
        ctx.gty.tokens = {Admin = tok }
        local tok, msg = Gateway:login(ctx, Gateway.Data.Users1[1])
        Test(tok, table.unpack(msg))
        ctx.gty.tokens.User = tok;
    end
end)

GtyUserAccessToken('UsingInvalidTokens', 'Verifies that attempt to access routes with invalid tokens is access denied')
:run(function(ctx)
    local jwt = Jwt(ctx.gty.tokens.Admin)
    -- by changing even a single byte in the token, it become invalid
    jwt.payload.iat = jwt.payload.iat + 1
    local token = ('Bearer %s.%s.%s'):format(
        Base64:encode(Json:encode(jwt.header)),
        Base64:encode(Json:encode(jwt.payload)),
        jwt.signature)
    -- use an invalid token to access a resource
    local resp = Http(ctx.gty('/_admin/about'), {
        method = 'GET',
        headers =  {
            Authorization = token
        }
    })
    V(resp):IsStatus(Http.Unauthorized, "Using a modified token should return Unauthorized status")

    -- using a token that does not have access to a resource (or route). e.g a normal user
    -- does not have access to admin routes
    resp = Http(ctx.gty('/_admin/about'), {
        method = 'GET',
        headers = {
            Authorization = ctx.gty.tokens.User
        }
    })
    V(resp):IsStatus(Http.Unauthorized, "Using token that does not have permision to access resource is denial")
    Equal(resp.body, 'Access to resource denied.', 'Respose must note that access to resource is denied')
end)
:attrs({reset = true})

GtyUserAccessToken('UsingValidTokens', 'Tests using valid tokens to access protected resources')
:run(function(ctx)
    local resp = Http(ctx.gty('/_admin/about'), {
        method = 'GET',
        headers = {
            Authorization = ctx.gty.tokens.Admin
        }
    })
    V(resp):IsStatus(Http.Ok, 'Accessing resource with valid token must be allowed')
end)

return GtyUserAccessToken
