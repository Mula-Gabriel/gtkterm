# Correctif conclude
- Ajout de `import sys` et `sys.exit(conclude())` dans test_macros_facultatifs.py et test_projet_nom.py.
- test_macros_facultatifs : OK sans autre changement.
- test_projet_nom : 2 echecs apres ajout (« application terminee », « code de sortie nul : -15 ») : le scenario finissait par `e.finish(quit=False)` alors que l'application tournait encore (quit=False suppose qu'elle a deja quitte). Cause : scenario. Correctif : `e.finish()` (app.quit, projet non modifie apres project-save). Aucun code produit change.
- Commandes : meson test -C build --suite gui test_macros_facultatifs (OK), ... test_projet_nom (OK apres correctif), meson test -C build : 51 OK, 0 echec.
