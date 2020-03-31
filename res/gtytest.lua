-- application configuration
app = {
    -- configure logging
    logging = {
        -- enable trace logging
        verbose = 0,
        -- enable logging to a file
        dir = '/tmp/semausu/gateway'
    },

    --
    -- http endpoint name
    --
    http = {
        url = 'gty.semausu.com',
        server = {
            -- server port
            port = 10080
        }
    },

    --
    -- access control list
    --
    secrets = {
        -- key used to generate user salt
        passwdkey = "8xF1nfomq1u5LzVB"
    },

    --
    -- application jwt configuration
    --
    jwt = {
        -- key used encode JWT's
        key    = 'fTjWnZr4u7x!A%C*F-JaNdRgUkXp2s5v',
        -- JWT exipry time in seconds, default 15 minutes
        expiry = 900,
        -- JWT realm
        realm  = '',
        -- JWT domain
        domain = 'gty.semausu.com',
        -- JWT path
        path   = ''
    },

    --
    -- postgres database configuration
    --
    postgres = {
        -- postgres connection parameters
        connect = {
            -- determines whether to read connection string from environment variable
            -- or not. should be set to name of environment variable. Useful in production
            env    = "",

            -- connection parameters which will be used to build connection string
            -- !!! UNSAFE - USE Only in development !!!
            dbname = "build",
            user   = "build",
            passwd = "passwd",
            host   = "postgres-db"
        },
        -- connection timeout in milliseconds
        timeout = 5000,
        -- time to keep connection alive in seconds
        keepAlive = 9000
    },

    --
    -- redis database configuration
    --
    redis = {
        -- connection parameters
        connect = {
            -- pick parameters from environment variable?
            env = "",
            -- specify parameters directly
            host = "redis-db",
            port = 6379
        },
        -- keep connections alive for 30 seconds
        keepAlive = 30000
    },

    --
    -- mail server settings
    --
    mail = {
        -- STMP mail server configuration
        stmp = {
            host = 'smtp4dev',
            port = 25,
            username = 'devops@suilteam.com',
            passwd = 'passwd'
        },

        -- Sender address
        sender = {
            -- email
            email = 'devops@suilteam.com',
            name  = 'DevOps Suilteam'
        }
    }
}