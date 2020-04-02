---
--- @module GatewayUsersRegister Fixture for testing the gateway
--- users endpoint i.e
--- POST /users/register
---

local Gateway = require("scripts/gateway") { }
local Http,_,V,Jwt = import("sys/http")

local GtyUsersRegister = Fixture('GatewayUsersRegister', "A collection of tests testing the '/users/register' route")

GtyUsersRegister:before(function(ctx)
    -- ensure that the server is running prior to running test
    if ctx.gty == nil or not Gateway:running() or ctx.attrs.reset then
        ctx.gty = Gateway:restart(Swept.Data.GtyBin, Swept.Data.GtyConfig, ctx.attrs.reset)
        Test(Gateway:init(ctx), 'Gateway must be successfully initialized before continuing test')
    end
end)

GtyUsersRegister('RegisterWithInvalidParameters', "Endpoint should reject registering when parameters are invalid")
:run(function(ctx)
    -- all required parameters missing
    local resp = Http(ctx.gty('/users/register'), {
        method = 'POST',
        form = {}
    })
    V(resp):IsStatus(Http.BadRequest, 'Request with empty parameters should return bad parameters')
    local json = resp:json()
    Equal(json.status, 'MissingFields', "Response should have a 'MissingFields' status")

    -- some required parameters missing
    resp = Http(ctx.gty('/users/register'), {
        method = 'POST',
        -- following form is missing password MUST be rejected
        form = { Email = 'email@example.com', FirstName = 'Email', LastName = 'Example'}
    })
    V(resp):IsStatus(Http.BadRequest, 'Request with empty parameters should return bad parameters')
    json = resp:json()
    Equal(json.status, 'MissingFields', "Response should have a 'MissingFields' status")

    -- Test various invalid data
    local data = {
        {
            Form = {Email = 'invalid@email',  FirstName = 'Invalid', LastName = 'Email', Passwd = 'passwd@123'},
            Expect = 'InvalidEmailAddress'
        },
        {
            Form = {Email = 'invalid',  FirstName = 'Invalid', LastName = 'Email', Passwd = 'passwd@123'},
            Expect = 'InvalidEmailAddress'
        },
        {
            Form = {Email = 'invalidEmail',  FirstName = 'Invalid', LastName = 'Email', Passwd = 'passwd@123'},
            Expect = 'InvalidEmailAddress'
        },
        {
            Form = {Email = 'valid@example.com',  FirstName = 'Valid', LastName = 'Example', Passwd = 'short'},
            Expect = 'InvalidPassword'
        }
    }
    for _,I in ipairs(data) do
        resp = Http(ctx.gty('/users/register'), {
            method = 'POST',
            form = I.Form
        })
        V(resp):IsStatus(Http.BadRequest, 'Request with invalid parameters should bad request status code')
        json = resp:json()
        Equal(json.status, I.Expect, "Response should have a '%s' status", I.Expect)
    end
end)
:attrs({reset = true})


GtyUsersRegister('RegisterWithValidParameters', "Tests registering users with valid input")
:run(function(ctx)
    -- tests registering valid users
    for _,user in ipairs(Gateway.Data.Users1) do
        local resp = Http(ctx.gty('/users/register'), {
            method = 'POST',
            form = user
        })
        V(resp):IsStatus(Http.Created, "User '%s' should have been successfully created", user.Email)
    end
end)


GtyUsersRegister('RegisterUserTwice', "Test registering users who have already been registered")
:run(function(ctx)
    -- Register valid users before test case
    for _,user in ipairs(Gateway.Data.Users2) do
        local resp = Http(ctx.gty('/users/register'), {
            method = 'POST',
            form = user
        })
        V(resp):IsStatus(Http.Created, "User '%s' should have been successfully created", user.Email)
    end

    -- attempt to register the same users again
    for _,user in ipairs(Gateway.Data.Users2) do
        local resp = Http(ctx.gty('/users/register'), {
            method = 'POST',
            form = user
        })
        V(resp):IsStatus(Http.BadRequest, "User '%s' must not be registred again", user.Email)
    end
end)

return GtyUsersRegister