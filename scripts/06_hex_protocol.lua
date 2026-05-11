-- 06_hex_protocol.lua
-- Protocole binaire sur port série : envoi de trames hex, attente d'ACK.
-- Exemple typique : Modbus RTU simplifié, ou tout protocole à trames fixes.
--
-- Trame de requête : 01 03 00 00 00 02 C4 0B  (Modbus Read Holding Registers)
-- Réponse attendue commence par : 01 03

local DEVICE = "/dev/ttyUSB0"
local BAUD   = 9600

local function bytes_to_hex(s)
    return (s:gsub(".", function(c)
        return string.format("%02X ", c:byte())
    end))
end

local ok, err = gtkterm.open(1, "serial", DEVICE, BAUD, 8, 1, 0, 0)
if not ok then
    gtkterm.log("Erreur : " .. err)
    return
end

gtkterm.log("Port ouvert, démarrage du test protocole binaire.")

-- Requête Modbus RTU (Read Holding Registers, addr=1, reg=0, count=2)
local REQUEST_HEX = "01 03 00 00 00 02 C4 0B"

local function send_request(n)
    gtkterm.log(string.format("--- Requête #%d ---", n))
    gtkterm.clear(1)

    local ok_send = gtkterm.send_hex(1, REQUEST_HEX)
    gtkterm.log("TX hex : " .. REQUEST_HEX .. (ok_send and " [OK]" or " [ERREUR]"))

    -- Attendre le début de la réponse Modbus (device address + function code)
    -- La réponse à Read HR commence par l'octet d'adresse (0x01) et le code fonction (0x03)
    -- On attend ici le caractère "\x01\x03" comme marqueur de début
    local resp = gtkterm.wait_for(1, "\x01\x03", 2000)
    if not resp then
        gtkterm.log("TIMEOUT — pas de réponse.")
        return nil
    end

    -- Lire le reste de la trame (5 octets restants pour 2 registres + CRC)
    gtkterm.sleep(50)  -- laisser arriver les octets restants
    local tail = gtkterm.read(1)
    local frame = resp .. tail

    gtkterm.log(string.format("RX hex : %s(%d octets)", bytes_to_hex(frame), #frame))

    -- Décoder les deux registres (octets 3-4 et 5-6 de la réponse)
    if #frame >= 7 then
        local reg1 = frame:byte(4) * 256 + frame:byte(5)
        local reg2 = frame:byte(6) * 256 + frame:byte(7)
        gtkterm.log(string.format("Registre 0 = %d (0x%04X)", reg1, reg1))
        gtkterm.log(string.format("Registre 1 = %d (0x%04X)", reg2, reg2))
        return reg1, reg2
    else
        gtkterm.log("Trame incomplète (" .. #frame .. " octets).")
        return nil
    end
end

-- Envoyer 5 requêtes avec 1 s d'intervalle et sauvegarder en CSV
local csvfile = "/tmp/modbus_log.csv"
gtkterm.save_csv(csvfile, "n", "reg0", "reg1", "timestamp")

for i = 1, 5 do
    local r0, r1 = send_request(i)
    if r0 then
        gtkterm.save_csv(csvfile, i, r0, r1, os.date("%H:%M:%S"))
    end
    gtkterm.sleep(1000)
end

gtkterm.close(1)
gtkterm.log("Résultats dans " .. csvfile)
