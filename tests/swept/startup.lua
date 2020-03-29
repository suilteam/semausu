---
--- @script Used to configure swept testing environment for Gateway testing
---

package.path = package.path..";scripts/?.lua"

local function Parse(p)
    p:option('--gtyconf', "Path to the configuration to be used when starting gateway", nil)
    p:option('--gtybin',  "Path to the gateway binary that will under test", nil)
    p:option('-S --server', "Url to gty-test server", 'http://localhost')
    p:option('-P --port',   "The port on which gty-test server is accepting request", '10084')
end

local function Init(parsed)
    return {
        GtyConfig = parsed.gtyconf,
        GtyBin    = parsed.gtybin,
        GtyServer = parsed.server,
        GtyPort   = parsed.port
    }
end

return function() return Parse, Init end