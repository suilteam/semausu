---
--- @test Tests the gateway init application
---

local Gateway = require("scripts/gateway") { }
local Http,_,V,Jwt = import("sys/http")

local GatewayInit = Fixture('GatewayInit', "A collection of test cases testing application initialization")

-- install before and after test handlers
GatewayInit:before(function(ctx)
    -- ensure that the server is running before executing test
    if ctx.gty == nil or not Gateway:running() then
        ctx.gty = Gateway:restart(Swept.Data.GtyBin, Swept.Data.GtyConfig, true)
    end
end):after(function(ctx)
    -- optimize to not stop server
end)

local TestUsers = {
    Administrator = {
        Email     = 'admin@suilteam.com',
        FirstName = 'Admin',
        LastName  = 'Gateway',
        Passwd    = 'admin123'
    }
}

---
--- @test
---
GatewayInit("AccessUnInitializedGateway", "Tests accessing an uninitialized gateway")
:run(function(ctx)
    -- valid routes shouldn't be accessible prior to initialization
    local resp = Http(ctx.gty('/users/register'), {
        method = 'POST',
        form = {Email = 'email@example.com', Passwd = 'SecretPassword'}
    })
    V(resp):IsStatus(Http.ServiceUnavailable, "All routes should returned 'Service Unavailable' status prior to server initialization")
end)

---
--- @test
---
GatewayInit("InitializingWithInvalidArguments", "Test initializing application with invalid arguments")
:run(function(ctx)
    -- initialization with empty body should return invalid status
    local resp = Http(ctx.gty('/app-init'), {
        method = 'POST'
    })
    V(resp):IsStatus(Http.BadRequest, "Gateway should return bad request when no upload data is provided")

    -- only json data should be accepted by the gateway
    local resp = Http(ctx.gty('/app-init'), {
        X = 'POST',
        body = "Hello world"
    })
    V(resp):IsStatus(Http.BadRequest, "Gateway should return bad request when invalid json data is provided")

    -- valid JSON but missing required Administrator account
    local resp = Http(ctx.gty('/app-init'), {
        method = 'POST',
        body = {}
    })
    V(resp):IsStatus(Http.BadRequest, "Gateway should return bad request when provided JSON data is missing required fields")

    -- Valid JSON with missing required Attributes
    local resp = Http(ctx.gty('/app-init'), {
        method = 'POST',
        body = {
            Administrator = {}
        }
    })
    V(resp):IsStatus(Http.BadRequest, "Gateway should return bad request when no upload data is provided")
end)

GatewayInit("InitializingWithValidArguments", "Test initializing an application with valid arguments")
:run(function(ctx)
    -- intialize application with valid parameters
    local resp = Http(ctx.gty('/app-init'), {
        method = 'POST',
        body = {
            Administrator = Gateway.Data.Admin
        }
    })
    V(resp):IsStatus(Http.Ok, "Initializing gateway with valid parameters should succeed")

    -- try initializing again with valid parameters, route should not be accessible
    resp  = Http(ctx.gty("/app-init"), {
        method = 'POST',
        body = {
            Administrator = Gateway.Data.Admin
        }
    })
    V(resp):IsStatus(Http.NotFound, "Initializing an already initialized gateway should fail as the route get disabled");

end)
:after(function(ctx)
    -- stop gateway server
    Gateway:stop();
end)

GatewayInit("AccessingRoutesAfterInitialization", "Verifies that routes are accessible after initialization")
:run(function(ctx)
    local resp = Http(ctx.gty('/app-init'), {
        method = 'POST',
        body = {
            Administrator = Gateway.Data.Admin
        }
    })
    V(resp):IsStatus(Http.Ok, "Initializing gateway with valid parameters should succeed")

    -- the route /users/register route should now be available (will fail with BAD_REQUEST)
    local resp = Http(ctx.gty('/users/register'), {
        method = 'POST',
        form   = Gateway.Data.Admin
    })
    V(resp):IsStatus(Http.BadRequest, "System routes should be reachable after gateway is initialized")
    local json = resp:json()
    Equal(json.status, 'UserAlreadyRegistered', "Administrator user should already have been registered")

    -- login administrator account to verify it has been registered
    local resp = Http(ctx.gty('/users/login'), {
        method = 'POST',
        form   = {
            Email  = Gateway.Data.Admin.Email,
            Passwd = Gateway.Data.Admin.Passwd
        }
    })
    V(resp):IsStatus(Http.Ok, "Administrator should be able to log into gateway")
           :HasHeader('Authorization', "A token should have been issued to admin user")

    local jwt = Jwt(resp.headers.Authorization)
    Test(jwt ~= nil, "Returned token should be a valid JWT")
    Test(jwt:AnyRole('SystemAdmin'), "Administrator should have a 'SystemAdmin' role")
end)
:after(function(ctx)
    -- stop gateway server
    Gateway:stop()
end)

return GatewayInit
