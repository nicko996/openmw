# Design — Freccia del personaggio rotante sulla mappa Lua (UIReboot)

Data: 2026-06-28
Branch engine: `feature/lua-paperdoll-widget`
Repo coinvolti:
- Engine: `OpenMW_Paper_Doll_Addon/openmw` (modifiche C++ + skin MyGUI)
- Mod: `UIReboot` (`scripts/ui_reboot/views/panels/map_panel.lua`)

## Problema

Il pannello mappa di UIReboot disegna oggi il marker "you are here" come un quadrato
non direzionale (`panPlayer` in `map_panel.lua:205`). Vogliamo usare lo stesso
puntatore della mappa vanilla — la freccia `textures/compass.dds` — e farla ruotare
secondo la direzione in cui guarda il personaggio.

Il blocco è lato engine: la mappa vanilla ruota la freccia con
`MyGUI::RotatingSkin::setAngle()` (`mwgui/mapwindow.cpp:533`), ma il widget `Image`
del Lua UI (`components/lua_ui/image.cpp`) usa una basis-skin `LuaTileRect` (per il
tiling) e **non espone alcuna proprietà di rotazione**. A livello di skin MyGUI
`RotatingSkin` e `LuaTileRect` sono basis-skin mutuamente esclusive. Inoltre
l'angolo corretto per gli **interni** richiede la correzione `mAngle` che l'engine
applica in `mwrender/localmap.cpp:478` e che non è esposta a Lua.

## Scope deciso

Parità completa con la mappa vanilla: la freccia deve puntare correttamente su
**mappa mondo, locale esterno e locale interno** (incluse le celle la cui mappa è
ruotata rispetto al nord). Questo richiede sia la primitiva di rotazione sia
l'esposizione della direzione corretta del player calcolata dall'engine.

## Approccio scelto

Approccio 1: `rotation` come **proprietà generica** del widget Image (estende
`LuaImage`, niente nuovo tipo di widget) + helper `playerArrowAngle()` sui binding
mappa che calcola l'angolo finale nell'engine (riusando la matematica vanilla).

Motivazione: `rotation` è la primitiva giusta — come `color`/`alpha`, una proprietà
del widget, riutilizzabile oltre la mappa (bussole, quadranti). L'helper engine
mantiene la correttezza (specie la correzione interni) dentro l'engine, lasciando il
Lua banale.

## Architettura

```
Engine A (rendering)   LuaImage: nuova prop `rotation` (radianti) -> RotatingSkin
Engine B (dato)        LuaWorldMap / LuaLocalMap: playerArrowAngle() -> radianti
Lua (UIReboot)         panPlayer: Image(compass.dds) con rotation = map:playerArrowAngle()
```

Data flow: ad ogni rebuild della mappa, `panPlayer` legge l'angolo dall'engine e lo
passa come `rotation` al widget Image. Col menu mappa aperto il gioco è in pausa,
quindi l'orientamento è statico mentre si guarda e aggiornato alla riapertura/refresh.

## Componente 1 — Engine A: proprietà `rotation` sul widget Image

File: `files/data/mygui/openmw_lua.xml`, `components/lua_ui/image.cpp`,
`components/lua_ui/image.hpp`.

1. Nuova skin MyGUI in `openmw_lua.xml`:

   ```xml
   <Resource type="ResourceSkin" name="LuaImageRotating" size="16 16">
       <BasisSkin type="RotatingSkin" offset="0 0 16 16" align="Stretch"/>
   </Resource>
   ```

   `RotatingSkin` è già definita in `files/data/mygui/core.skin` ed è una basis-skin
   nativa MyGUI. Un `ImageBox` con `RotatingSkin` accetta `setImageTexture` /
   `setImageCoord` come quello con `LuaTileRect` (è il pattern del compass vanilla
   in `openmw_map_window.layout`), quindi la pipeline texture esistente continua a
   funzionare.

2. `LuaImage::allUsedProperties()`: aggiungere `"rotation"` alla lista.

3. `LuaImage::updateProperties()`:
   - Determinare la skin desiderata in base alla **presenza** della prop `rotation`
     nella tabella di proprietà (presenza, non valore != 0, per evitare flicker /
     switch di skin quando l'angolo passa per lo zero). Si verifica la presenza sulla
     sol-table delle proprietà.
   - Se la skin corrente differisce da quella desiderata, chiamare
     `changeWidgetSkin("LuaImageRotating")` (o `"LuaImage"`) e riaggiornare il
     puntatore di subwidget: `mTileRect = dynamic_cast<LuaTileRect*>(getSubWidgetMain())`
     (sarà `nullptr` in modalità rotante).
   - Riapplicare texture/resource (il blocco esistente). In modalità rotante saltare
     il blocco di tiling (guardia `if (mTileRect)`); `color` resta invariata.
   - **Dopo** `WidgetExtension::updateProperties()` (così la size del widget è già
     applicata): in modalità rotante, `getSubWidgetMain()->castType<MyGUI::RotatingSkin>()`
     -> `setCenter(IntPoint(width/2, height/2))` + `setAngle(rotation)`.

4. `image.hpp`: nessun nuovo membro indispensabile oltre a `mTileRect` (già presente);
   `mTileRect` ora può essere `nullptr` -> tutte le sue dereferenze vanno guardate.

Vincoli/limiti noti:
- Tiling e rotazione sono **alternativi** (un'immagine rotante non supporta il
  tiling). Documentato nei commenti.
- Il centro di rotazione viene fissato a metà della size corrente in
  `updateProperties`; se il widget viene ridimensionato senza ri-trigger di
  `updateProperties` il centro resta stale. Accettabile per la freccia (size fissa).
- Le immagini esistenti (senza prop `rotation`) non cambiano skin né comportamento:
  nessuna regressione attesa.

Rischio implementativo principale: lo switch di skin a runtime via `changeWidgetSkin`
ricrea i subwidget e può azzerare texture/UV; per questo la riapplicazione della
texture avviene sempre dopo lo switch, all'interno dello stesso `updateProperties`.

## Componente 2 — Engine B: `playerArrowAngle()` sui binding mappa

File: `apps/openmw/mwrender/localmap.hpp/.cpp`, `apps/openmw/mwlua/mapbindings.hpp/.cpp`,
`apps/openmw/mwlua/uibindings.cpp`.

1. In `MWRender::LocalMap` estrarre la matematica della direzione (oggi inline in
   `updatePlayer`, `localmap.cpp:478`) in un getter pubblico const:

   ```cpp
   osg::Vec3f getPlayerDirection(const osg::Quat& orientation) const;
   // interni:  orientation * Quat(mAngle, (0,0,-1)).inverse() * Vec3(0,1,0)
   // esterni:  orientation * Vec3(0,1,0)
   ```

   `updatePlayer` viene rifattorizzato per chiamare questo getter (DRY: unica fonte
   di verità per la direzione). `mInterior`/`mAngle` sono membri già impostati durante
   `requestMap`, quindi il getter const è sicuro.

2. In `mapbindings`:
   - `LuaLocalMap::playerArrowAngle() const`: orientamento dal player come in
     `WindowManager::updateMap` (`windowmanagerimp.cpp:873`):
     `osg::Quat orientation(-player.rot[2], osg::Vec3(0,0,1))`; poi
     `dir = mLocalMap->getPlayerDirection(orientation)`; ritorna `std::atan2(dir.x(), dir.y())`.
   - `LuaWorldMap::playerArrowAngle() const`: stesso orientamento, `dir = orientation * Vec3(0,1,0)`
     (la mappa mondo ha sempre il nord in alto, nessuna correzione); ritorna
     `std::atan2(dir.x(), dir.y())`.

   Entrambi ritornano radianti, lo stesso valore che la mappa vanilla passa a
   `RotatingSkin::setAngle`. Le texture della mappa Lua sono pubblicate con
   `flipV=true` esattamente come la mappa vanilla, quindi la convenzione angolare
   coincide e non serve alcun segno aggiuntivo (da verificare visivamente nei test).

3. In `uibindings.cpp`, esporre come metodi usertype:
   ```cpp
   worldMap["playerArrowAngle"] = [](const LuaWorldMap& m) { return m.playerArrowAngle(); };
   localMap["playerArrowAngle"] = [](const LuaLocalMap& m) { return m.playerArrowAngle(); };
   ```

## Componente 3 — Lua: `panPlayer` in map_panel.lua

File: `UIReboot/scripts/ui_reboot/views/panels/map_panel.lua`.

1. Costante texture in cima al modulo (pattern di `DOOR_TEX`/`CUSTOM_TEX`):
   ```lua
   local COMPASS_TEX = (function()
       local ok, t = pcall(ui.texture, { path = 'textures/compass.dds' }); return ok and t or nil
   end)()
   ```

2. `panPlayer(out, movable, pan, cx, cy, angle)`:
   - Se `COMPASS_TEX` è presente: un singolo widget `ui.TYPE.Image` 32×32 (size nativa
     del compass), centrato su `(cx, cy)`, con `resource = COMPASS_TEX` e
     `rotation = angle`. Registrato come `movable` (pan come gli altri marker).
   - Fallback se `COMPASS_TEX` manca: il quadrato non direzionale attuale (comportamento
     odierno preservato).

3. Chiamanti:
   - World (`worldContent`): `panPlayer(..., cx, cy, ST.world:playerArrowAngle())`.
   - Local (`localContent`): `panPlayer(..., w/2, h/2, ST.localMap:playerArrowAngle())`.

   La lettura dell'angolo avviene al build/rebuild, coerente con l'architettura di
   refresh esistente.

## Testing

Build dell'engine, avvio del gioco con UIReboot attivo, apertura mappa.

1. **Mappa mondo**: la freccia è sulla posizione del player e punta verso il facing;
   girando il personaggio (e riaprendo la mappa, gioco in pausa) la freccia segue.
2. **Locale esterno**: idem.
3. **Locale interno con mappa di cella ruotata** (caso critico della correzione
   `mAngle`): la freccia punta correttamente rispetto a corridoi/stanze, non storta.
4. **Confronto vanilla**: a parità di facing, la freccia UIReboot punta come quella
   della mappa vanilla.
5. **Regressione**: immagini Lua esistenti senza `rotation` invariate (render +
   tiling) — verificare schermate inventario/magia/paperdoll.

## Out of scope

- Animazione/interpolazione della rotazione (la freccia è statica a menu aperto).
- Esposizione di un angolo "camera" distinto dallo yaw dell'attore (vanilla usa lo
  yaw dell'attore; ci allineiamo a quello).
- Marker direzionali per i door-marker o le note custom (restano non direzionali).
