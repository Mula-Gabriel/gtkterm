-- 02_at_commands.lua
-- Dialogue AT avec un modem sur port série.
-- Ouvre un second port (handle 1) indépendamment du port principal GTKTerm.
--
-- Usage : adapter DEVICE et BAUD selon votre matériel.

local DEVICE = "/dev/ttyUSB0"
local BAUD   = 115200

-- Séquence de commandes AT à exécuter dans l'ordre
local commands = {
    { cmd = "AT\r",          expect = "OK",  desc = "Ping modem"          },
    { cmd = "AT+GMR\r",      expect = "OK",  desc = "Version firmware"    },
    { cmd = "AT+CMGF=1\r",   expect = "OK",  desc = "Mode SMS texte"      },
    { cmd = "AT+CSQ\r",      expect = "OK",  desc = "Qualité signal"      },
    { cmd = "AT+CREG?\r",    expect = "OK",  desc = "Statut réseau"       },
}

-- Ouvrir le port série (handle 1)
local ok, err = gtkterm.open(1, "serial", DEVICE, BAUD, 8, 1, 0, 0)
if not ok then
    gtkterm.log("Erreur ouverture " .. DEVICE .. " : " .. err)
    return
end
gtkterm.log("Port ouvert : " .. DEVICE .. " @ " .. BAUD)

-- Vider le buffer de réception avant de commencer
gtkterm.clear(1)

local function send_at(cmd_entry)
    gtkterm.log(">>> " .. cmd_entry.desc)
    gtkterm.log("    TX : " .. cmd_entry.cmd:gsub("\r", "<CR>"))
    gtkterm.send(1, cmd_entry.cmd)

    local resp = gtkterm.wait_for(1, cmd_entry.expect, 3000)
    if resp then
        -- Afficher la réponse sur une seule ligne (retirer les \r\n)
        local clean = resp:gsub("[\r\n]+", " "):match("^%s*(.-)%s*$")
        gtkterm.log("    RX : " .. clean)
    else
        gtkterm.log("    TIMEOUT — pas de réponse '" .. cmd_entry.expect .. "'")
    end

    gtkterm.sleep(200)
end

for _, entry in ipairs(commands) do
    send_at(entry)
end

gtkterm.close(1)
gtkterm.log("Port fermé.")
