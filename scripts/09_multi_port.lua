-- 09_multi_port.lua
-- Démontre l'utilisation de plusieurs ports simultanément :
--   - Port 0  : port principal GTKTerm (déjà ouvert)
--   - Port 1  : second port série (ex. /dev/ttyUSB1)
--   - Port 2  : client TCP (ex. passerelle réseau)
--
-- Cas d'usage : relayer des données d'un équipement série vers un serveur TCP.

local SERIAL_DEV  = "/dev/ttyUSB1"
local SERIAL_BAUD = 115200
local TCP_HOST    = "127.0.0.1"
local TCP_PORT    = 5000
local DURATION_S  = 30   -- durée totale du relai en secondes

-- Ouvrir le port série additionnel
do
    local ok, err = gtkterm.open(1, "serial", SERIAL_DEV, SERIAL_BAUD, 8, 1, 0, 0)
    if not ok then
        gtkterm.log("Impossible d'ouvrir " .. SERIAL_DEV .. " : " .. err)
        return
    end
    gtkterm.log("Port série ouvert : " .. SERIAL_DEV)
end

-- Ouvrir la connexion TCP
do
    local ok, err = gtkterm.open(2, "tcp_client", TCP_HOST, TCP_PORT)
    if not ok then
        gtkterm.log("Impossible de se connecter à " .. TCP_HOST .. ":" .. TCP_PORT .. " : " .. err)
        gtkterm.close(1)
        return
    end
    gtkterm.log("TCP connecté : " .. TCP_HOST .. ":" .. TCP_PORT)
end

gtkterm.log(string.format("Relai actif pendant %d s...", DURATION_S))
gtkterm.log("Port 0 (principal) → Port 2 (TCP)")
gtkterm.log("Port 1 (série ext) → Port 0 (principal)")

local deadline = gtkterm.time() + DURATION_S
local forwarded = 0

while gtkterm.time() < deadline do
    -- Port 0 → Port 2 : ce que l'utilisateur tape dans GTKTerm part en TCP
    local from_main = gtkterm.read(0)
    if #from_main > 0 then
        gtkterm.send(2, from_main)
        forwarded = forwarded + #from_main
    end

    -- Port 1 → Port 0 : données du second périphérique affichées dans GTKTerm
    local from_serial = gtkterm.read(1)
    if #from_serial > 0 then
        gtkterm.send(0, from_serial)
        forwarded = forwarded + #from_serial
    end

    -- Port 2 → Port 0 : réponses TCP affichées dans GTKTerm
    local from_tcp = gtkterm.read(2)
    if #from_tcp > 0 then
        gtkterm.send(0, from_tcp)
        forwarded = forwarded + #from_tcp
    end

    gtkterm.sleep(10)
end

gtkterm.close(2)
gtkterm.close(1)
gtkterm.log(string.format("Relai terminé. %d octet(s) transférés.", forwarded))
