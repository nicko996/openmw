# Lua Map Player-Arrow Rotation — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Far ruotare il puntatore del personaggio (la freccia `compass.dds` vanilla) sulla mappa di UIReboot, secondo la direzione in cui guarda il personaggio, su mappa mondo, locale esterno e locale interno.

**Architecture:** Si aggiunge una proprietà generica `rotation` (radianti) al widget `Image` del Lua UI (resa con una skin MyGUI `RotatingSkin`, alternativa al tiling) e un helper `playerArrowAngle()` ai binding `LuaWorldMap`/`LuaLocalMap` che calcola l'angolo finale nell'engine (riusando la matematica di `LocalMap::updatePlayer`, inclusa la correzione `mAngle` degli interni). Il Lua di UIReboot disegna un `Image` con `compass.dds` e `rotation = map:playerArrowAngle()`.

**Tech Stack:** C++ (OpenMW engine, MyGUI), XML skin MyGUI, Lua (OpenMW Lua UI / mod UIReboot). Build: CMake + Visual Studio 17 2022 (`MSVC2022_64/OpenMW.sln`).

## Global Constraints

- Repo engine: `c:/Users/nicko/Documents/Claude/Projects/OpenMW_Paper_Doll_Addon/openmw` (Task 1 e 2, branch `feature/lua-paperdoll-widget`).
- Repo mod: `c:/Users/nicko/Documents/Claude/Projects/UIReboot` (Task 3, repo git separato).
- Build dir: `MSVC2022_64/`. Config di esempio: `RelWithDebInfo` (usa la config che lanci di solito).
- Comando build (da `MSVC2022_64/`): `cmake --build . --config RelWithDebInfo --target openmw` (la prima build può richiedere minuti; le incrementali sono rapide).
- Le risorse runtime sono copie in `MSVC2022_64/RelWithDebInfo/resources/vfs/...`; le modifiche a `files/data/mygui/*.xml` vanno ricopiate lì (passo esplicito nel Task 1).
- `rotation` è in **radianti** (come `MyGUI::RotatingSkin::setAngle` e `std::atan2(x, y)` vanilla). Tiling e rotazione sono mutuamente esclusivi.
- Non rompere le immagini Lua esistenti: senza la prop `rotation` il widget Image deve comportarsi esattamente come oggi.
- Verifica = build + avvio del gioco con UIReboot attivo + osservazione (non esiste un unit harness per il rendering MyGUI / i binding Lua interattivi).

---

### Task 1: Engine A — proprietà `rotation` sul widget Image

**Files:**
- Modify: `files/data/mygui/openmw_lua.xml` (aggiunta skin `LuaImageRotating`)
- Modify: `components/lua_ui/image.hpp`
- Modify: `components/lua_ui/image.cpp`
- Test (temporaneo, sostituito dal Task 3): `c:/Users/nicko/Documents/Claude/Projects/UIReboot/scripts/ui_reboot/views/panels/map_panel.lua` (funzione `panPlayer`)

**Interfaces:**
- Consumes: niente (primo task).
- Produces: il widget `ui.TYPE.Image` accetta una nuova proprietà `rotation` (number, radianti). Se presente, l'immagine viene ruotata attorno al proprio centro; in quel caso `tileH`/`tileV` non hanno effetto.

- [ ] **Step 1: Aggiungere la skin rotante a `openmw_lua.xml`**

Inserire la nuova risorsa subito dopo il blocco `LuaImage` (dopo la riga 6):

```xml
    <Resource type="ResourceSkin" name="LuaImageRotating" size="16 16">
        <BasisSkin type="RotatingSkin" offset="0 0 16 16" align="Stretch"/>
    </Resource>
```

(Il tipo basis-skin `RotatingSkin` è nativo MyGUI ed è già usato in `files/data/mygui/core.skin`.)

- [ ] **Step 2: Dichiarare i nuovi membri/metodi in `image.hpp`**

Sostituire il corpo della classe `LuaImage` (righe 26-35) con:

```cpp
    class LuaImage : public MyGUI::ImageBox, public WidgetExtension
    {
        MYGUI_RTTI_DERIVED(LuaImage)

    protected:
        void initialize() override;
        void updateProperties() override;
        const std::vector<std::string_view>& allUsedProperties() const override;

        // Switches between the default tiling skin and the rotating skin, refreshing mTileRect
        // (null while rotating). No-op if already in the requested mode.
        void setRotating(bool rotating);
        // Sets the rotation centre (widget midpoint) and angle on the RotatingSkin. No-op unless rotating.
        void applyRotation(float angle);

        LuaTileRect* mTileRect = nullptr;
        bool mRotating = false;
    };
```

- [ ] **Step 3: Implementare la logica di rotazione in `image.cpp`**

Aggiungere l'include in cima (dopo `#include <MyGUI_RenderManager.h>`):

```cpp
#include <MyGUI_RotatingSkin.h>

#include <cmath>
#include <limits>
```

Sostituire `LuaImage::initialize()` (righe 31-36) con:

```cpp
    void LuaImage::initialize()
    {
        changeWidgetSkin("LuaImage");
        mTileRect = dynamic_cast<LuaTileRect*>(getSubWidgetMain());
        mRotating = false;
        WidgetExtension::initialize();
    }

    void LuaImage::setRotating(bool rotating)
    {
        if (rotating == mRotating)
            return;
        changeWidgetSkin(rotating ? "LuaImageRotating" : "LuaImage");
        // LuaTileRect only exists on the default skin; null while rotating.
        mTileRect = dynamic_cast<LuaTileRect*>(getSubWidgetMain());
        mRotating = rotating;
    }

    void LuaImage::applyRotation(float angle)
    {
        if (!mRotating)
            return;
        auto* rot = getSubWidgetMain()->castType<MyGUI::RotatingSkin>(false);
        if (rot == nullptr)
            return;
        const MyGUI::IntSize size = getSize();
        rot->setCenter(MyGUI::IntPoint(size.width / 2, size.height / 2));
        rot->setAngle(angle);
    }
```

Sostituire `LuaImage::updateProperties()` (righe 38-85) con (la presenza della prop `rotation` è rilevata con un sentinel NaN, perché `mProperties` è privato in `WidgetExtension`):

```cpp
    void LuaImage::updateProperties()
    {
        // Decide rotation by PRESENCE of the property, not its value: a NaN sentinel means "absent".
        // Deciding by presence avoids a skin flip when the angle happens to pass through 0.
        const float rotation = propertyValue("rotation", std::numeric_limits<float>::quiet_NaN());
        setRotating(!std::isnan(rotation));

        // Dynamic texture path (e.g. character preview / map RTT texture).
        // Takes priority over the normal VFS-path resource.
        TextureResource* resource = propertyValue<TextureResource*>("resource", nullptr);
        if (resource && resource->mDynamicTexture)
        {
            setRenderItemTexture(resource->mDynamicTexture);
            getSubWidgetMain()->_setUVSet(resource->mFlipV
                    ? MyGUI::FloatRect(0.f, 1.f, 1.f, 0.f)
                    : MyGUI::FloatRect(0.f, 0.f, 1.f, 1.f));
            setColour(propertyValue("color", MyGUI::Colour(1, 1, 1, 1)));
            WidgetExtension::updateProperties();
            applyRotation(rotation);
            return;
        }

        // Reset UVs in case this widget previously displayed a flipped dynamic resource.
        getSubWidgetMain()->_setUVSet(MyGUI::FloatRect(0.f, 0.f, 1.f, 1.f));
        deleteAllItems();
        MyGUI::IntCoord atlasCoord;
        if (resource)
        {
            atlasCoord
                = MyGUI::IntCoord(static_cast<int>(resource->mOffset.x()), static_cast<int>(resource->mOffset.y()),
                    static_cast<int>(resource->mSize.x()), static_cast<int>(resource->mSize.y()));
            setImageTexture(resource->mPath);
        }

        bool tileH = propertyValue("tileH", false);
        bool tileV = propertyValue("tileV", false);

        MyGUI::ITexture* texture = MyGUI::RenderManager::getInstance().getTexture(_getTextureName());
        MyGUI::IntSize textureSize;
        if (texture != nullptr)
            textureSize = MyGUI::IntSize(texture->getWidth(), texture->getHeight());

        if (atlasCoord.width == 0)
            atlasCoord.width = textureSize.width;
        if (atlasCoord.height == 0)
            atlasCoord.height = textureSize.height;

        // Tiling is only available on the default skin (no LuaTileRect while rotating).
        if (mTileRect != nullptr)
            mTileRect->updateSize(MyGUI::IntSize(tileH ? atlasCoord.width : 0, tileV ? atlasCoord.height : 0));
        setImageTile(atlasCoord.size());
        setImageCoord(atlasCoord);

        setColour(propertyValue("color", MyGUI::Colour(1, 1, 1, 1)));

        WidgetExtension::updateProperties();

        // After the base class has applied position/size, so the centre matches the final size.
        applyRotation(rotation);
    }
```

Nota: la riga originale del path dinamico NON chiamava `setColour`; qui lo aggiungiamo prima del `return` (default bianco `(1,1,1,1)` = nessun tint, comportamento invariato per le texture dinamiche esistenti).

- [ ] **Step 4: Aggiungere `"rotation"` alle proprietà usate**

In `LuaImage::allUsedProperties()` (riga 90) cambiare:

```cpp
            std::vector<std::string_view> props = { "resource", "tileH", "tileV", "color" };
```

in:

```cpp
            std::vector<std::string_view> props = { "resource", "tileH", "tileV", "color", "rotation" };
```

- [ ] **Step 5: Build dell'engine**

Run (da `MSVC2022_64/`):
```
cmake --build . --config RelWithDebInfo --target openmw
```
Expected: build completata senza errori (`Build succeeded`).

- [ ] **Step 6: Copiare la skin XML aggiornata nelle risorse runtime**

Run (da `MSVC2022_64/`):
```
cp ../files/data/mygui/openmw_lua.xml RelWithDebInfo/resources/vfs/mygui/openmw_lua.xml
```
Expected: nessun output (copia riuscita). Verifica: `grep LuaImageRotating RelWithDebInfo/resources/vfs/mygui/openmw_lua.xml` deve stampare la riga della nuova skin.

- [ ] **Step 7: Verifica temporanea della rotazione (rimossa nel Task 3)**

In `UIReboot/.../map_panel.lua`, sostituire TEMPORANEAMENTE il corpo di `panPlayer` (righe 205-231) con una freccia compass a 45° fissi, per provare che la prop `rotation` ruota davvero:

```lua
local function panPlayer(out, movable, pan, cx, cy)
    local size = theme.scale(32)
    local bx = math.floor(cx - size / 2)
    local by = math.floor(cy - size / 2)
    local node = { type = ui.TYPE.Image, props = {
        resource = ui.texture { path = 'textures/compass.dds' },
        position = v2(bx + pan.x, by + pan.y), size = v2(size, size),
        rotation = 0.785,  -- ~45° (TEMP: verifica Task 1)
    } }
    out[#out + 1] = node
    movable[#movable + 1] = { props = node.props, bx = bx, by = by }
end
```

- [ ] **Step 8: Avviare il gioco e verificare**

Run (da `MSVC2022_64/RelWithDebInfo/`): `./openmw.exe`
Poi: carica un salvataggio, apri la mappa (World e Local).
Expected:
- Al posto del quadrato compare la freccia `compass.dds`, **ruotata di ~45°**.
- Nessun crash; le altre immagini delle schermate (inventario/magia/paperdoll) sono invariate (regressione: il widget Image senza `rotation` rende e fa tiling come prima).

- [ ] **Step 9: Ripristinare `panPlayer` originale (prima del commit dell'engine)**

Annullare la modifica temporanea del Step 7 (`git checkout` nel repo UIReboot, oppure ripristinare il corpo originale di `panPlayer`). Il Task 3 riscriverà `panPlayer` nella versione definitiva. L'engine va committato senza la modifica temporanea Lua.

- [ ] **Step 10: Commit (repo engine)**

Run (da `c:/Users/nicko/Documents/Claude/Projects/OpenMW_Paper_Doll_Addon/openmw`):
```bash
git add files/data/mygui/openmw_lua.xml components/lua_ui/image.hpp components/lua_ui/image.cpp
git commit -m "lua_ui: add rotation property to the Image widget

Backed by a MyGUI RotatingSkin (LuaImageRotating skin), selected when the
rotation property is present. Mutually exclusive with tiling. Lets Lua UI
render a rotated image (e.g. the vanilla compass marker on the map).

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Engine B — `playerArrowAngle()` sui binding mappa

**Files:**
- Modify: `apps/openmw/mwrender/localmap.hpp` (dichiarazione getter)
- Modify: `apps/openmw/mwrender/localmap.cpp` (implementazione + refactor `updatePlayer`)
- Modify: `apps/openmw/mwlua/mapbindings.hpp` (dichiarazioni)
- Modify: `apps/openmw/mwlua/mapbindings.cpp` (implementazioni)
- Modify: `apps/openmw/mwlua/uibindings.cpp` (esposizione usertype)

**Interfaces:**
- Consumes: niente del Task 1 (indipendente lato C++).
- Produces:
  - `MWRender::LocalMap::getPlayerDirection(const osg::Quat& orientation) const -> osg::Vec3f`
  - `MWLua::LuaWorldMap::playerArrowAngle() const -> float` (radianti)
  - `MWLua::LuaLocalMap::playerArrowAngle() const -> float` (radianti)
  - Lua: `worldMap:playerArrowAngle()` e `localMap:playerArrowAngle()` ritornano un number (radianti) pronto per la prop `rotation`.

- [ ] **Step 1: Dichiarare `getPlayerDirection` in `localmap.hpp`**

Dopo la dichiarazione di `updatePlayer` (riga 79-80), aggiungere:

```cpp
        /// Player facing direction projected onto the map plane. Interior maps include the cell's
        /// map-rotation correction (mAngle); exteriors are north-up. Mirrors updatePlayer's direction.
        osg::Vec3f getPlayerDirection(const osg::Quat& orientation) const;
```

(`osg/Quat` e `osg/BoundingBox` sono già inclusi; `osg::Vec3f` è disponibile transitivamente.)

- [ ] **Step 2: Implementare `getPlayerDirection` e rifattorizzare `updatePlayer` in `localmap.cpp`**

Inserire l'implementazione del getter subito prima di `LocalMap::updatePlayer` (prima della riga 478):

```cpp
    osg::Vec3f LocalMap::getPlayerDirection(const osg::Quat& orientation) const
    {
        if (mInterior)
        {
            osg::Quat cameraOrient(mAngle, osg::Vec3(0, 0, -1));
            return orientation * cameraOrient.inverse() * osg::Vec3f(0, 1, 0);
        }
        return orientation * osg::Vec3f(0, 1, 0);
    }
```

Poi sostituire l'inizio di `updatePlayer` (righe 478-501) — il blocco che calcola `direction` viene rimpiazzato da una sola chiamata al getter, mantenendo invariata la matematica di `x,y,u,v`:

Da:
```cpp
    void LocalMap::updatePlayer(const osg::Vec3f& position, const osg::Quat& orientation, float& u, float& v, int& x,
        int& y, osg::Vec3f& direction)
    {
        // retrieve the x,y grid coordinates the player is in
        osg::Vec2f pos(position.x(), position.y());

        if (mInterior)
        {
            worldToInteriorMapPosition(pos, u, v, x, y);

            osg::Quat cameraOrient(mAngle, osg::Vec3(0, 0, -1));
            direction = orientation * cameraOrient.inverse() * osg::Vec3f(0, 1, 0);
        }
        else
        {
            direction = orientation * osg::Vec3f(0, 1, 0);

            x = static_cast<int>(std::ceil(pos.x() / mMapWorldSize) - 1);
            y = static_cast<int>(std::ceil(pos.y() / mMapWorldSize) - 1);

            // convert from world coordinates to texture UV coordinates
            u = std::abs((pos.x() - (mMapWorldSize * x)) / mMapWorldSize);
            v = 1.0f - std::abs((pos.y() - (mMapWorldSize * y)) / mMapWorldSize);
        }
```

A:
```cpp
    void LocalMap::updatePlayer(const osg::Vec3f& position, const osg::Quat& orientation, float& u, float& v, int& x,
        int& y, osg::Vec3f& direction)
    {
        direction = getPlayerDirection(orientation);

        // retrieve the x,y grid coordinates the player is in
        osg::Vec2f pos(position.x(), position.y());

        if (mInterior)
        {
            worldToInteriorMapPosition(pos, u, v, x, y);
        }
        else
        {
            x = static_cast<int>(std::ceil(pos.x() / mMapWorldSize) - 1);
            y = static_cast<int>(std::ceil(pos.y() / mMapWorldSize) - 1);

            // convert from world coordinates to texture UV coordinates
            u = std::abs((pos.x() - (mMapWorldSize * x)) / mMapWorldSize);
            v = 1.0f - std::abs((pos.y() - (mMapWorldSize * y)) / mMapWorldSize);
        }
```

(Il resto di `updatePlayer` — il loop fog-of-war dalla riga 503 in poi — resta invariato.)

- [ ] **Step 3: Dichiarare `playerArrowAngle()` in `mapbindings.hpp`**

In `class LuaWorldMap`, nella sezione `public`, dopo `std::vector<Marker> markers() const;` (riga 60) aggiungere:

```cpp
        float playerArrowAngle() const; // radians, ready for the Image `rotation` property
```

In `class LuaLocalMap`, nella sezione `public`, dopo `MapPosition worldToMap(float x, float y) const;` (riga 121) aggiungere:

```cpp
        float playerArrowAngle() const; // radians, ready for the Image `rotation` property
```

- [ ] **Step 4: Implementare i due metodi in `mapbindings.cpp`**

Aggiungere l'include in cima (dopo `#include <osg/Texture2D>`):

```cpp
#include <osg/Quat>
```

Implementare `LuaWorldMap::playerArrowAngle()` subito dopo `LuaWorldMap::markers()` (dopo la riga 131):

```cpp
    float LuaWorldMap::playerArrowAngle() const
    {
        MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        if (player.isEmpty())
            return 0.f;
        // Same orientation construction as WindowManager::updateMap (yaw around Z).
        const float yaw = player.getRefData().getPosition().rot[2];
        osg::Quat orientation(-yaw, osg::Vec3(0, 0, 1));
        // World map is always north-up: no interior correction.
        osg::Vec3f dir = orientation * osg::Vec3f(0, 1, 0);
        return std::atan2(dir.x(), dir.y());
    }
```

Implementare `LuaLocalMap::playerArrowAngle()` subito dopo `LuaLocalMap::worldToMap()` (dopo la riga 275):

```cpp
    float LuaLocalMap::playerArrowAngle() const
    {
        if (!mLocalMap)
            return 0.f;
        MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        if (player.isEmpty())
            return 0.f;
        // Same orientation construction as WindowManager::updateMap (yaw around Z).
        const float yaw = player.getRefData().getPosition().rot[2];
        osg::Quat orientation(-yaw, osg::Vec3(0, 0, 1));
        // getPlayerDirection applies the interior map-rotation correction when needed.
        osg::Vec3f dir = mLocalMap->getPlayerDirection(orientation);
        return std::atan2(dir.x(), dir.y());
    }
```

- [ ] **Step 5: Esporre i metodi usertype in `uibindings.cpp`**

Dopo `worldMap["worldToImage"] = ...` (riga 519) aggiungere:

```cpp
            worldMap["playerArrowAngle"] = [](const LuaWorldMap& m) { return m.playerArrowAngle(); };
```

Dopo il blocco `localMap["worldToMap"] = ...` (dopo la riga 567) aggiungere:

```cpp
            localMap["playerArrowAngle"] = [](const LuaLocalMap& m) { return m.playerArrowAngle(); };
```

- [ ] **Step 6: Build dell'engine**

Run (da `MSVC2022_64/`):
```
cmake --build . --config RelWithDebInfo --target openmw
```
Expected: build completata senza errori.

- [ ] **Step 7: Verifica del valore dell'angolo (sanity check via log)**

In `UIReboot/.../map_panel.lua`, dentro `worldContent`, subito prima della chiamata `panPlayer(...)` (riga ~520), aggiungere TEMPORANEAMENTE:

```lua
print('[arrow] world angle =', ST.world:playerArrowAngle())
```
e dentro `localContent`, prima del `panPlayer(...)` finale (riga ~603):
```lua
print('[arrow] local angle =', ST.localMap:playerArrowAngle())
```

Run il gioco, apri la mappa, gira il personaggio (chiudi/riapri la mappa) in esterno e in interno.
Expected (in `openmw.log` o console): un number in radianti (circa in `[-pi, pi]`) che **cambia** quando il personaggio cambia direzione; in interno ruotato il valore riflette la correzione (diverso dall'angolo "grezzo" dello yaw). Nessun errore Lua.
Poi rimuovere le due righe `print`.

- [ ] **Step 8: Commit (repo engine)**

Run (da `c:/Users/nicko/Documents/Claude/Projects/OpenMW_Paper_Doll_Addon/openmw`):
```bash
git add apps/openmw/mwrender/localmap.hpp apps/openmw/mwrender/localmap.cpp apps/openmw/mwlua/mapbindings.hpp apps/openmw/mwlua/mapbindings.cpp apps/openmw/mwlua/uibindings.cpp
git commit -m "mwlua: expose playerArrowAngle() on world/local map bindings

Returns the player-facing angle (radians) for the map arrow, mirroring the
vanilla map computation. Factors the direction math out of LocalMap::updatePlayer
into getPlayerDirection() so interiors keep the cell map-rotation correction.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Lua — freccia rotante in `map_panel.lua`

**Files:**
- Modify: `c:/Users/nicko/Documents/Claude/Projects/UIReboot/scripts/ui_reboot/views/panels/map_panel.lua`

**Interfaces:**
- Consumes: prop `rotation` sul widget Image (Task 1); `ST.world:playerArrowAngle()` e `ST.localMap:playerArrowAngle()` (Task 2).
- Produces: la freccia del personaggio sulla mappa è `compass.dds` orientata secondo il facing del personaggio (mondo, esterno, interno). Nessuna nuova API esposta.

- [ ] **Step 1: Aggiungere la texture compass in cima al modulo**

Dopo `CUSTOM_TEX` (riga 41) aggiungere:

```lua
-- Vanilla directional player marker (rotates with the player's facing).
local COMPASS_TEX = (function() local ok, t = pcall(ui.texture, { path = 'textures/compass.dds' }); return ok and t or nil end)()
```

- [ ] **Step 2: Riscrivere `panPlayer` per accettare un angolo e ruotare la freccia**

Sostituire l'intera funzione `panPlayer` (righe 202-231 — il commento descrittivo + la funzione) con:

```lua
-- The "you are here" marker. When the vanilla compass texture is available it is drawn as a
-- directional arrow rotated by `angle` (radians, from <map>:playerArrowAngle()). If the texture is
-- missing, falls back to a non-directional gold-outlined square with a centre dot.
local function panPlayer(out, movable, pan, cx, cy, angle)
    if COMPASS_TEX then
        local size = theme.scale(32)  -- compass.dds is 32x32
        local bx = math.floor(cx - size / 2)
        local by = math.floor(cy - size / 2)
        local node = { type = ui.TYPE.Image, props = {
            resource = COMPASS_TEX,
            position = v2(bx + pan.x, by + pan.y), size = v2(size, size),
            rotation = angle or 0,
        } }
        out[#out + 1] = node
        movable[#movable + 1] = { props = node.props, bx = bx, by = by }
        return
    end

    -- fallback: non-directional square (previous behaviour)
    local size   = theme.scale(16)
    local border = theme.scale(2)
    local dot    = theme.scale(6)
    local bx = math.floor(cx - size / 2)
    local by = math.floor(cy - size / 2)
    local node = {
        type = ui.TYPE.Widget,
        props = { position = v2(bx + pan.x, by + pan.y), size = v2(size, size) },
        content = ui.content {
            { type = ui.TYPE.Image, props = {
                resource = ui.texture { path = theme.WHITE }, color = theme.PLAYER,
                relativeSize = v2(1, 1) } },
            { type = ui.TYPE.Image, props = {
                resource = ui.texture { path = theme.WHITE }, color = theme.PANEL2, alpha = 0.5,
                position = v2(border, border), size = v2(size - border * 2, size - border * 2) } },
            { type = ui.TYPE.Image, props = {
                resource = ui.texture { path = theme.WHITE }, color = theme.PLAYER,
                relativePosition = v2(0.5, 0.5), anchor = v2(0.5, 0.5), size = v2(dot, dot) } },
        },
    }
    out[#out + 1] = node
    movable[#movable + 1] = { props = node.props, bx = bx, by = by }
end
```

- [ ] **Step 3: Passare l'angolo dal world map**

In `worldContent`, nella sezione "you-are-here", sostituire (riga ~520):

```lua
            panPlayer(out, movable, pan, ox + img.x * scale, oy + img.y * scale)
```

con:

```lua
            panPlayer(out, movable, pan, ox + img.x * scale, oy + img.y * scale, world:playerArrowAngle())
```

(`world` è la variabile locale già impostata a `ST.world` all'inizio di `worldContent`.)

- [ ] **Step 4: Passare l'angolo dal local map**

In `localContent`, alla fine, sostituire (riga ~603):

```lua
    if m then
        panPlayer(out, movable, pan, w / 2, h / 2)
    end
```

con:

```lua
    if m then
        panPlayer(out, movable, pan, w / 2, h / 2, lm:playerArrowAngle())
    end
```

(`lm` è la variabile locale già impostata a `ST.localMap` all'inizio di `localContent`.)

- [ ] **Step 5: Avviare il gioco e verificare (mondo + esterno)**

Run (da `MSVC2022_64/RelWithDebInfo/`): `./openmw.exe` con UIReboot attivo.
Carica un salvataggio in esterno, apri la mappa.
Expected:
- **Mappa mondo**: la freccia è sulla posizione del player e punta verso la direzione in cui guarda; girando il personaggio (chiudi/riapri mappa) la freccia segue.
- **Locale esterno**: idem.
- A parità di facing, la freccia punta come quella della mappa **vanilla**.

- [ ] **Step 6: Verificare gli interni (caso critico `mAngle`)**

Entra in un interno la cui mappa è ruotata rispetto al nord (molte celle interne lo sono), apri la mappa, scope "Local".
Expected: la freccia punta correttamente rispetto a corridoi/stanze (allineata alla mappa vanilla dello stesso interno), non storta.

- [ ] **Step 7: Verifica di regressione**

Apri le schermate inventario / magia / paperdoll.
Expected: immagini e icone invariate; nessuna immagine ruotata per errore; nessun crash. (Conferma che il widget Image senza `rotation` è inalterato.)

- [ ] **Step 8: Commit (repo UIReboot)**

Run (da `c:/Users/nicko/Documents/Claude/Projects/UIReboot`):
```bash
git add scripts/ui_reboot/views/panels/map_panel.lua
git commit -m "map: rotate the player arrow with the vanilla compass marker

panPlayer now draws textures/compass.dds rotated by <map>:playerArrowAngle()
(world and local, interiors corrected). Falls back to the square marker if the
compass texture is unavailable.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Self-Review

**1. Spec coverage:**
- Engine A (rotation prop su Image) → Task 1. ✓ (skin `LuaImageRotating`, prop `rotation` per presenza, `setRotating`/`applyRotation`, tiling guardato, `allUsedProperties`).
- Engine B (`playerArrowAngle()` + getter `mAngle`/direzione) → Task 2. ✓ (`getPlayerDirection` + refactor `updatePlayer`, due binding, esposizione usertype).
- Lua integration (compass + fallback + due chiamanti) → Task 3. ✓
- Testing (mondo/esterno/interno ruotato + regressione) → Task 3 Step 5-7; rotazione isolata → Task 1 Step 7-8; angolo → Task 2 Step 7. ✓
- Out of scope (no animazione, yaw attore, marker porte non direzionali) → rispettato (nessun task li tocca). ✓

**2. Placeholder scan:** Nessun "TBD/TODO". Tutti gli step di codice mostrano il codice completo. Le verifiche temporanee (Task 1 Step 7, Task 2 Step 7) hanno snippet completi e step espliciti di rimozione. ✓

**3. Type consistency:**
- `getPlayerDirection(const osg::Quat&) const -> osg::Vec3f`: dichiarato (T2 S1), implementato (T2 S2), usato (T2 S4 in `LuaLocalMap::playerArrowAngle`). ✓
- `playerArrowAngle() const -> float`: dichiarato (T2 S3), implementato (T2 S4), esposto (T2 S5), consumato in Lua come `world:playerArrowAngle()` / `lm:playerArrowAngle()` (T3 S3-S4). ✓
- `panPlayer(out, movable, pan, cx, cy, angle)`: nuova firma con `angle` (T3 S2), chiamata coerente con 6 argomenti (T3 S3-S4). ✓
- Prop `rotation` (number radianti) prodotta da T1, usata in `panPlayer` (T3 S2) col valore di `playerArrowAngle()` (T2). ✓
- `setRotating`/`applyRotation`/`mRotating`/`mTileRect`: dichiarati (T1 S2) e usati coerentemente (T1 S3). ✓

Nessuna incoerenza trovata.
