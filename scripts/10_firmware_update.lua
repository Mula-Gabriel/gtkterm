-- 10_firmware_update.lua
-- Simule une mise à jour firmware via protocole texte simple :
--   1. Vérifier la version courante
--   2. Passer en mode bootloader
--   3. Envoyer les pages de firmware (ici simulées)
--   4. Vérifier le CRC de chaque page
--   5. Confirmer la mise à jour
--
-- Ce script montre la gestion d'erreurs et les retries.

local DEVICE  = "/dev/ttyUSB0"
local BAUD    = 115200
local RETRIES = 3

local function open_port()
    local ok, err = gtkterm.open(1, "serial", DEVICE, BAUD, 8, 1, 0, 0)
    if not ok then
        gtkterm.log("FATAL : " .. err)
        return false
    end
    gtkterm.clear(1)
    return true
end

local function send_cmd(cmd, expect, timeout_ms, retries)
    retries = retries or 1
    for attempt = 1, retries do
        gtkterm.send(1, cmd .. "\r\n")
        local r = gtkterm.wait_for(1, expect, timeout_ms or 2000)
        if r then
            return r
        end
        if attempt < retries then
            gtkterm.log(string.format("  Retry %d/%d pour '%s'...", attempt, retries, cmd))
            gtkterm.sleep(500)
            gtkterm.clear(1)
        end
    end
    return nil
end

-- Données firmware simulées (en vrai : lire un fichier binaire)
local function make_fake_pages(n)
    local pages = {}
    for i = 1, n do
        -- Chaque page : 16 octets aléatoires encodés en hex
        local hex = ""
        local crc = 0
        for _ = 1, 16 do
            local b = math.random(0, 255)
            hex = hex .. string.format("%02X", b)
            crc = (crc + b) % 256
        end
        pages[i] = { hex = hex, crc = string.format("%02X", crc) }
    end
    return pages
end

math.randomseed(os.time())

if not open_port() then return end

-- Étape 1 : version
gtkterm.log("=== Étape 1 : Version ===")
local ver = send_cmd("VERSION", "VER:", 2000, RETRIES)
if not ver then
    gtkterm.log("Équipement ne répond pas — abandon.")
    gtkterm.close(1)
    return
end
gtkterm.log("Version courante : " .. ver:match("VER:([^\r\n]+)") or "?")

-- Étape 2 : passer en mode bootloader
gtkterm.log("=== Étape 2 : Mode bootloader ===")
local r = send_cmd("BOOTLOADER", "READY", 3000, RETRIES)
if not r then
    gtkterm.log("L'équipement n'est pas passé en mode bootloader.")
    gtkterm.close(1)
    return
end
gtkterm.log("Mode bootloader actif.")

-- Étape 3 : envoi des pages
local pages = make_fake_pages(8)
gtkterm.log(string.format("=== Étape 3 : Envoi %d pages ===", #pages))

local errors = 0
for i, page in ipairs(pages) do
    local cmd = string.format("PAGE %d %s", i, page.hex)
    local ack = send_cmd(cmd, "ACK", 2000, RETRIES)
    if ack then
        -- Vérifier le CRC retourné
        local remote_crc = ack:match("ACK%s+([%x]+)")
        if remote_crc and remote_crc:upper() == page.crc:upper() then
            gtkterm.log(string.format("  Page %d/%d OK  CRC=%s", i, #pages, page.crc))
        else
            gtkterm.log(string.format("  Page %d/%d CRC MISMATCH local=%s remote=%s",
                i, #pages, page.crc, remote_crc or "?"))
            errors = errors + 1
        end
    else
        gtkterm.log(string.format("  Page %d/%d TIMEOUT", i, #pages))
        errors = errors + 1
    end
end

-- Étape 4 : finalisation
gtkterm.log("=== Étape 4 : Finalisation ===")
if errors == 0 then
    local done = send_cmd("COMMIT", "OK", 5000, RETRIES)
    if done then
        gtkterm.log("Mise à jour réussie !")
    else
        gtkterm.log("COMMIT échoué.")
    end
else
    gtkterm.log(string.format("Abandon : %d erreur(s) de page.", errors))
    send_cmd("ABORT", "OK", 2000)
end

gtkterm.close(1)
gtkterm.log("Port fermé.")
