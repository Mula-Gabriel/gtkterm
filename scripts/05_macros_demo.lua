-- 05_macros_demo.lua
-- Montre comment lister et déclencher les macros GTKTerm depuis un script.

gtkterm.log("=== Démo macros ===")

-- Charger les listes de valeurs disponibles (pour les args %#NomListe)
local lists = gtkterm.get_lists()
local list_map = {}  -- name -> {entries}
for _, l in ipairs(lists) do
    list_map[l.name] = l.entries
    gtkterm.log(string.format("Liste '%s' : %d entrée(s)", l.name, #l.entries))
    for _, e in ipairs(l.entries) do
        gtkterm.log(string.format("  '%s' => '%s'", e.display, e.value))
    end
end

local macros = gtkterm.get_macros()

if #macros == 0 then
    gtkterm.log("Aucune macro définie.")
    return
end

gtkterm.log(string.format("%d macro(s) :", #macros))
for _, m in ipairs(macros) do
    if m.n_args == 0 then
        gtkterm.log(string.format("  [%d] %s", m.index, m.label))
    else
        local descs = {}
        for _, a in ipairs(m.args) do
            local name = a.label ~= "" and a.label or a.type
            if a.list ~= "" then name = name .. "[" .. a.list .. "]" end
            table.insert(descs, name)
        end
        gtkterm.log(string.format("  [%d] %s  (%s)", m.index, m.label,
                                  table.concat(descs, ", ")))
    end
end

-- Construire les valeurs par défaut pour une macro en utilisant la 1re entrée
-- de chaque liste si disponible
local function default_args(m)
    local vals = {}
    for _, a in ipairs(m.args) do
        if a.list ~= "" and list_map[a.list] and #list_map[a.list] > 0 then
            table.insert(vals, list_map[a.list][1].value)
        elseif a.type == "f" or a.type == "lf" or a.type == "g" then
            table.insert(vals, "1.0")
        else
            table.insert(vals, "1")
        end
    end
    return vals
end

-- Envoyer la première macro
local first = macros[1]
gtkterm.log(string.format("Envoi macro [%d] '%s'...", first.index, first.label))
local vals = default_args(first)
if #vals > 0 then
    gtkterm.log("  args: " .. table.concat(vals, ", "))
end
local ok, err = gtkterm.send_macro(first.index, table.unpack(vals))
gtkterm.log(ok and "  OK" or ("  Erreur: " .. (err or "?")))

gtkterm.sleep(500)

-- Envoyer par label
gtkterm.log("Envoi par label '" .. first.label .. "'...")
local ok2, err2 = gtkterm.send_macro(first.label, table.unpack(vals))
gtkterm.log(ok2 and "  OK" or ("  Erreur: " .. (err2 or "?")))

-- Envoyer toutes les macros (avec 1re valeur de liste par défaut)
gtkterm.log("Envoi séquentiel...")
for _, m in ipairs(macros) do
    local v = default_args(m)
    gtkterm.log("  → " .. m.label .. (#v > 0 and (" [" .. table.concat(v,",") .. "]") or ""))
    gtkterm.send_macro(m.index, table.unpack(v))
    gtkterm.sleep(500)
end

gtkterm.log("=== Fin démo macros ===")
