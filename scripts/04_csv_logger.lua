-- 04_csv_logger.lua
-- Capture des mesures envoyées par un capteur série et les enregistre en CSV.
--
-- Format attendu sur le port série, une ligne par mesure :
--   TEMP:23.4,HUM:61.2\n
--
-- Le CSV résultant contiendra : timestamp, température, humidité

local DEVICE  = "/dev/ttyUSB0"
local BAUD    = 9600
local OUTFILE = "/tmp/mesures.csv"
local N_SAMPLES = 20        -- nombre de mesures à collecter
local TIMEOUT_MS = 10000    -- 10 s par mesure

-- En-tête CSV (créé une seule fois)
local f = io.open(OUTFILE, "r")
if not f then
    gtkterm.save_csv(OUTFILE, "timestamp", "temperature", "humidite")
    gtkterm.log("CSV créé : " .. OUTFILE)
else
    f:close()
    gtkterm.log("CSV existant, ajout en fin de fichier : " .. OUTFILE)
end

local ok, err = gtkterm.open(1, "serial", DEVICE, BAUD, 8, 1, 0, 0)
if not ok then
    gtkterm.log("Erreur : " .. err)
    return
end

gtkterm.log(string.format("Collecte de %d mesures sur %s...", N_SAMPLES, DEVICE))
gtkterm.clear(1)

local count = 0
while count < N_SAMPLES do
    -- Attendre une ligne complète (terminée par \n)
    local line = gtkterm.wait_for(1, "\n", TIMEOUT_MS)
    if not line then
        gtkterm.log("Timeout — arrêt de la collecte.")
        break
    end

    -- Parser "TEMP:23.4,HUM:61.2"
    local temp = line:match("TEMP:([%d%.%-]+)")
    local hum  = line:match("HUM:([%d%.%-]+)")

    if temp and hum then
        local ts = os.date("%Y-%m-%d %H:%M:%S")
        gtkterm.save_csv(OUTFILE, ts, temp, hum)
        count = count + 1
        gtkterm.log(string.format("[%d/%d] T=%-6s H=%-6s", count, N_SAMPLES, temp, hum))
    else
        gtkterm.log("Ligne non reconnue : " .. line:gsub("[\r\n]", ""))
    end
end

gtkterm.close(1)
gtkterm.log(string.format("Collecte terminée. %d mesures dans %s", count, OUTFILE))
