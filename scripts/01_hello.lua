-- 01_hello.lua
-- Démonstration basique : log, sleep, envoi sur le port principal (handle 0).
-- Le handle 0 est toujours le port ouvert dans GTKTerm — aucun open() requis.

gtkterm.log("=== Hello GTKTerm ===")

for i = 1, 5 do
    gtkterm.log("tick " .. i)
    gtkterm.sleep(500)
end

-- Envoyer une chaîne sur le port principal
gtkterm.send(0, "Hello from Lua!\r\n")
gtkterm.log("Envoyé sur le port principal.")

-- log accepte n'importe quel type
gtkterm.log(42)
gtkterm.log(true)
gtkterm.log(nil)

gtkterm.log("=== Terminé ===")
