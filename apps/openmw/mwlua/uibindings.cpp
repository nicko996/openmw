#include "uibindings.hpp"

#include <components/lua/util.hpp>
#include <components/lua_ui/alignment.hpp>
#include <components/lua_ui/content.hpp>
#include <components/lua_ui/element.hpp>
#include <components/lua_ui/layers.hpp>
#include <components/lua_ui/registerscriptsettings.hpp>
#include <components/lua_ui/resources.hpp>
#include <components/lua_ui/util.hpp>

#include <components/misc/strings/format.hpp>
#include <components/settings/values.hpp>

#include "characterpreviewwrapper.hpp"
#include "context.hpp"
#include "luamanagerimp.hpp"
#include "mapbindings.hpp"
#include "object.hpp"

#include <components/esm3/loadnpc.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwrender/renderingmanager.hpp"
#include "../mwworld/class.hpp"

namespace MWLua
{
    namespace
    {
        const std::unordered_map<MWGui::GuiMode, std::string_view> modeToName{
            { MWGui::GM_Inventory, "Interface" },
            { MWGui::GM_Container, "Container" },
            { MWGui::GM_Companion, "Companion" },
            { MWGui::GM_MainMenu, "MainMenu" },
            { MWGui::GM_Journal, "Journal" },
            { MWGui::GM_Scroll, "Scroll" },
            { MWGui::GM_Book, "Book" },
            { MWGui::GM_Alchemy, "Alchemy" },
            { MWGui::GM_Repair, "Repair" },
            { MWGui::GM_Dialogue, "Dialogue" },
            { MWGui::GM_Barter, "Barter" },
            { MWGui::GM_Rest, "Rest" },
            { MWGui::GM_SpellBuying, "SpellBuying" },
            { MWGui::GM_Travel, "Travel" },
            { MWGui::GM_SpellCreation, "SpellCreation" },
            { MWGui::GM_Enchanting, "Enchanting" },
            { MWGui::GM_Recharge, "Recharge" },
            { MWGui::GM_Training, "Training" },
            { MWGui::GM_MerchantRepair, "MerchantRepair" },
            { MWGui::GM_Levelup, "LevelUp" },
            { MWGui::GM_Name, "ChargenName" },
            { MWGui::GM_Race, "ChargenRace" },
            { MWGui::GM_Birth, "ChargenBirth" },
            { MWGui::GM_Class, "ChargenClass" },
            { MWGui::GM_ClassGenerate, "ChargenClassGenerate" },
            { MWGui::GM_ClassPick, "ChargenClassPick" },
            { MWGui::GM_ClassCreate, "ChargenClassCreate" },
            { MWGui::GM_Review, "ChargenClassReview" },
            { MWGui::GM_Loading, "Loading" },
            { MWGui::GM_LoadingWallpaper, "LoadingWallpaper" },
            { MWGui::GM_Jail, "Jail" },
            { MWGui::GM_QuickKeysMenu, "QuickKeysMenu" },
        };

        const auto nameToMode = [] {
            std::unordered_map<std::string_view, MWGui::GuiMode> res;
            for (const auto& [mode, name] : modeToName)
                res[name] = mode;
            return res;
        }();
    }

    sol::table registerUiApi(const Context& context)
    {
        sol::state_view lua = context.sol();
        bool menu = context.mType == Context::Menu;

        MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();

        sol::table api(lua, sol::create);
        api["_setHudVisibility"] = [luaManager = context.mLuaManager](bool state) {
            luaManager->addAction([state] { MWBase::Environment::get().getWindowManager()->setHudVisibility(state); });
        };
        api["_isHudVisible"] = []() -> bool { return MWBase::Environment::get().getWindowManager()->isHudVisible(); };
        api["_getDefaultFontSize"] = []() -> int { return Settings::gui().mFontSize; };
        api["showMessage"]
            = [luaManager = context.mLuaManager](std::string_view message, const sol::optional<sol::table>& options) {
                  MWGui::ShowInDialogueMode mode = MWGui::ShowInDialogueMode_IfPossible;
                  if (options.has_value())
                  {
                      auto showInDialogue = options->get<sol::optional<bool>>("showInDialogue");
                      if (showInDialogue.has_value())
                      {
                          if (*showInDialogue)
                              mode = MWGui::ShowInDialogueMode_Only;
                          else
                              mode = MWGui::ShowInDialogueMode_Never;
                      }
                  }
                  luaManager->addUIMessage(message, mode);
              };

        api["_showInteractiveMessage"] = [windowManager](std::string_view message, sol::optional<sol::table>) {
            windowManager->interactiveMessageBox(message, { "#{Interface:OK}" });
        };
        api["CONSOLE_COLOR"] = LuaUtil::makeStrictReadOnly(LuaUtil::tableFromPairs<std::string, Misc::Color>(lua,
            {
                { "Default", Misc::Color::fromHex(MWBase::WindowManager::sConsoleColor_Default.substr(1)) },
                { "Error", Misc::Color::fromHex(MWBase::WindowManager::sConsoleColor_Error.substr(1)) },
                { "Success", Misc::Color::fromHex(MWBase::WindowManager::sConsoleColor_Success.substr(1)) },
                { "Info", Misc::Color::fromHex(MWBase::WindowManager::sConsoleColor_Info.substr(1)) },
            }));
        api["printToConsole"]
            = [luaManager = context.mLuaManager](const std::string& message, const Misc::Color& color) {
                  luaManager->addInGameConsoleMessage(message + "\n", color);
              };
        api["setConsoleMode"] = [luaManager = context.mLuaManager, windowManager](std::string_view mode) {
            luaManager->addAction([mode = std::string(mode), windowManager] { windowManager->setConsoleMode(mode); });
        };
        api["getConsoleMode"] = [windowManager]() -> std::string_view { return windowManager->getConsoleMode(); };
        api["setConsoleSelectedObject"] = [luaManager = context.mLuaManager, windowManager](const sol::object& obj) {
            if (obj == sol::nil)
                luaManager->addAction([windowManager] { windowManager->setConsoleSelectedObject(MWWorld::Ptr()); });
            else
            {
                if (!obj.is<LObject>())
                    throw std::runtime_error("Game object expected");
                luaManager->addAction(
                    [windowManager, obj = obj.as<LObject>()] { windowManager->setConsoleSelectedObject(obj.ptr()); });
            }
        };
        api["content"] = LuaUi::loadContentConstructor(context.mLua);

        api["create"]
            = [luaManager = context.mLuaManager, menu](const sol::table& layout, sol::optional<sol::table> options) {
                  auto element = LuaUi::Element::make(layout, menu, options);
                  luaManager->addAction([element] { element->create(); }, "Create UI");
                  return element;
              };

        api["updateAll"] = [luaManager = context.mLuaManager, menu]() {
            LuaUi::Element::forEach(menu, [](LuaUi::Element* e) {
                if (e->mState == LuaUi::Element::Created)
                    e->mState = LuaUi::Element::Update;
            });
            luaManager->addAction([menu]() { LuaUi::Element::forEach(menu, [](LuaUi::Element* e) { e->update(); }); },
                "Update all menu UI elements");
        };
        api["_getMenuTransparency"] = []() -> float { return Settings::gui().mMenuTransparency; };

        sol::table layersTable(lua, sol::create);
        layersTable["indexOf"] = [](std::string_view name) -> sol::optional<size_t> {
            size_t index = LuaUi::Layer::indexOf(name);
            if (index == LuaUi::Layer::count())
                return sol::nullopt;
            else
                return LuaUtil::toLuaIndex(index);
        };
        layersTable["insertAfter"] = [context](std::string afterName, std::string name, const sol::object& opt) {
            LuaUi::Layer::Options options;
            options.mInteractive = LuaUtil::getValueOrDefault(LuaUtil::getFieldOrNil(opt, "interactive"), true);
            context.mLuaManager->addAction(
                [afterName = std::move(afterName), name = std::move(name), options]() {
                    size_t index = LuaUi::Layer::indexOf(afterName);
                    if (index == LuaUi::Layer::count())
                        throw std::logic_error(
                            Misc::StringUtils::format("Couldn't insert after non-existent layer %s", afterName));
                    LuaUi::Layer::insert(index + 1, name, options);
                },
                "Insert after UI layer");
        };
        layersTable["insertBefore"] = [context](std::string beforeName, std::string name, const sol::object& opt) {
            LuaUi::Layer::Options options;
            options.mInteractive = LuaUtil::getValueOrDefault(LuaUtil::getFieldOrNil(opt, "interactive"), true);
            context.mLuaManager->addAction(
                [beforeName = std::move(beforeName), name = std::move(name), options]() {
                    size_t index = LuaUi::Layer::indexOf(beforeName);
                    if (index == LuaUi::Layer::count())
                        throw std::logic_error(
                            Misc::StringUtils::format("Couldn't insert before non-existent layer %s", beforeName));
                    LuaUi::Layer::insert(index, name, options);
                },
                "Insert before UI layer");
        };
        sol::table layers = LuaUtil::makeReadOnly(layersTable);
        sol::table layersMeta = layers[sol::metatable_key];
        layersMeta[sol::meta_function::length] = []() { return LuaUi::Layer::count(); };
        layersMeta[sol::meta_function::index] = sol::overload(
            [](const sol::object& self, size_t index) {
                index = LuaUtil::fromLuaIndex(index);
                return LuaUi::Layer(index);
            },
            [layersTable](
                const sol::object& self, std::string_view key) { return layersTable.raw_get<sol::object>(key); });
        {
            auto pairs = [layers](const sol::object&) {
                auto next = [](const sol::table& l, size_t i) -> sol::optional<std::tuple<size_t, LuaUi::Layer>> {
                    if (i < LuaUi::Layer::count())
                        return std::make_tuple(i + 1, LuaUi::Layer(i));
                    else
                        return sol::nullopt;
                };
                return std::make_tuple(next, layers, 0);
            };
            layersMeta[sol::meta_function::pairs] = pairs;
            layersMeta[sol::meta_function::ipairs] = pairs;
        }
        api["layers"] = layers;

        sol::table typeTable(lua, sol::create);
        for (const auto& it : LuaUi::widgetTypeToName())
            typeTable.set(it.second, it.first);
        api["TYPE"] = LuaUtil::makeStrictReadOnly(typeTable);

        api["ALIGNMENT"] = LuaUtil::makeStrictReadOnly(LuaUtil::tableFromPairs<std::string_view, LuaUi::Alignment>(lua,
            { { "Start", LuaUi::Alignment::Start }, { "Center", LuaUi::Alignment::Center },
                { "End", LuaUi::Alignment::End } }));

        api["registerSettingsPage"] = &LuaUi::registerSettingsPage;
        api["removeSettingsPage"] = &LuaUi::removeSettingsPage;

        api["texture"] = [luaManager = context.mLuaManager](const sol::table& options) {
            LuaUi::TextureData data;
            sol::object path = LuaUtil::getFieldOrNil(options, "path");
            if (path.is<std::string>())
                data.mPath = VFS::Path::Normalized(path.as<std::string>());
            if (data.mPath.empty())
                throw std::logic_error("Invalid texture path");
            sol::object offset = LuaUtil::getFieldOrNil(options, "offset");
            if (offset.is<osg::Vec2f>())
                data.mOffset = offset.as<osg::Vec2f>();
            sol::object size = LuaUtil::getFieldOrNil(options, "size");
            if (size.is<osg::Vec2f>())
                data.mSize = size.as<osg::Vec2f>();
            return luaManager->uiResourceManager()->registerTexture(std::move(data));
        };

        api["screenSize"] = []() {
            return osg::Vec2f(
                static_cast<float>(Settings::video().mResolutionX), static_cast<float>(Settings::video().mResolutionY));
        };

        api["_getAllUiModes"] = [](sol::this_state thisState) {
            sol::table res(thisState, sol::create);
            for (const auto& [_, name] : modeToName)
                res[name] = name;
            return res;
        };
        api["_getUiModeStack"] = [windowManager](sol::this_state thisState) {
            sol::table res(thisState, sol::create);
            int i = 1;
            for (MWGui::GuiMode m : windowManager->getGuiModeStack())
                res[i++] = modeToName.at(m);
            return res;
        };
        api["_setUiModeStack"]
            = [windowManager, luaManager = context.mLuaManager](sol::table modes, sol::optional<LObject> arg) {
                  std::vector<MWGui::GuiMode> newStack(modes.size());
                  for (unsigned i = 0; i < newStack.size(); ++i)
                      newStack[i] = nameToMode.at(LuaUtil::cast<std::string_view>(modes[LuaUtil::toLuaIndex(i)]));
                  luaManager->addAction(
                      [windowManager, newStack = std::move(newStack), arg = std::move(arg)]() {
                          MWWorld::Ptr ptr;
                          if (arg.has_value())
                              ptr = arg->ptr();
                          const std::vector<MWGui::GuiMode>& stack = windowManager->getGuiModeStack();
                          size_t common = 0;
                          while (common < std::min(stack.size(), newStack.size()) && stack[common] == newStack[common])
                              common++;
                          // TODO: Maybe disallow opening/closing special modes (main menu, settings, loading screen)
                          // from player scripts. Add new Lua context "menu" that can do it.
                          for (size_t i = stack.size() - common; i > 0; i--)
                              windowManager->popGuiMode(true);
                          if (common == newStack.size() && !newStack.empty() && arg.has_value())
                              windowManager->pushGuiMode(newStack.back(), ptr);
                          for (size_t i = common; i < newStack.size(); ++i)
                              windowManager->pushGuiMode(newStack[i], ptr);
                      },
                      "Set UI modes");
              };
        api["_getAllWindowIds"] = [windowManager](sol::this_state thisState) {
            sol::table res(thisState, sol::create);
            for (std::string_view name : windowManager->getAllWindowIds())
                res[name] = name;
            return res;
        };
        api["_getAllowedWindows"] = [windowManager](sol::this_state thisState, std::string_view mode) {
            sol::table res(thisState, sol::create);
            for (std::string_view name : windowManager->getAllowedWindowIds(nameToMode.at(mode)))
                res[name] = name;
            return res;
        };
        api["_setWindowDisabled"]
            = [windowManager, luaManager = context.mLuaManager](std::string window, bool disabled) {
                  luaManager->addAction(
                      [=, window = std::move(window)]() { windowManager->setDisabledByLua(window, disabled); });
              };
        api["_isWindowVisible"]
            = [windowManager](std::string_view window) { return windowManager->isWindowVisible(window); };

        // TODO
        // api["_showMouseCursor"] = [](bool) {};

        api["newCharacterPreview"]
            = [luaManager = context.mLuaManager](const sol::table& options)
                  -> std::shared_ptr<LuaCharacterPreview> {
                  sol::object actorObj = LuaUtil::getFieldOrNil(options, "actor");
                  if (!actorObj.is<LObject>())
                      throw std::runtime_error("newCharacterPreview: 'actor' must be a game object");

                  MWWorld::Ptr actor = actorObj.as<LObject>().ptr();
                  if (actor.getType() != ESM::NPC::sRecordId)
                      throw std::runtime_error("newCharacterPreview: 'actor' must be an NPC");

                  int sizeX = 512;
                  int sizeY = 1024;
                  sol::object sizeObj = LuaUtil::getFieldOrNil(options, "size");
                  if (sizeObj.is<osg::Vec2f>())
                  {
                      const auto v = sizeObj.as<osg::Vec2f>();
                      sizeX = std::max(1, static_cast<int>(v.x()));
                      sizeY = std::max(1, static_cast<int>(v.y()));
                  }

                  osg::Group* root = MWBase::Environment::get().getWorld()->getRenderingManager()->getRootNode();
                  Resource::ResourceSystem* resourceSystem = MWBase::Environment::get().getResourceSystem();

                  auto preview = std::make_shared<LuaCharacterPreview>(
                      root, resourceSystem, actor, sizeX, sizeY);

                  LuaUi::TextureData data;
                  // mDynamicTexture stays null until doConstruct runs; the LuaImage widget
                  // tolerates this and will pick up the texture on its next updateProperties.
                  data.mFlipV = true;
                  data.mDynamicOwner = preview;
                  auto textureResource = luaManager->uiResourceManager()->registerTexture(std::move(data));
                  preview->setTextureResource(textureResource);

                  luaManager->addAction([preview] { preview->doConstruct(); }, "CharacterPreview construct");
                  return preview;
              };

        // -- World/local map API --
        // ui.newWorldMap() / ui.newLocalMap(); see files/lua_api/openmw/ui.lua for documentation.
        api["newWorldMap"] = [luaManager = context.mLuaManager]() -> std::shared_ptr<LuaWorldMap> {
            MWRender::GlobalMap* globalMap = MWBase::Environment::get().getWindowManager()->getGlobalMapRender();
            if (globalMap == nullptr)
                return nullptr;

            auto worldMap = std::make_shared<LuaWorldMap>(globalMap);

            LuaUi::TextureData base;
            base.mFlipV = true;
            worldMap->setBaseResource(luaManager->uiResourceManager()->registerTexture(std::move(base)));
            LuaUi::TextureData overlay;
            overlay.mFlipV = true;
            worldMap->setOverlayResource(luaManager->uiResourceManager()->registerTexture(std::move(overlay)));

            luaManager->addAction([worldMap] { worldMap->doConstruct(); }, "WorldMap construct");
            return worldMap;
        };

        api["newLocalMap"] = [luaManager = context.mLuaManager]() -> std::shared_ptr<LuaLocalMap> {
            MWRender::LocalMap* localMap = MWBase::Environment::get().getWindowManager()->getLocalMapRender();
            if (localMap == nullptr)
                return nullptr;
            return std::make_shared<LuaLocalMap>(localMap, luaManager->uiResourceManager());
        };

        return api;
    }

    sol::table initUserInterfacePackage(const Context& context)
    {
        if (context.initializeOnce("openmw_ui_usertypes"))
        {
            auto uiElement = context.sol().new_usertype<LuaUi::Element>("UiElement");
            uiElement[sol::meta_function::to_string] = [](const LuaUi::Element& element) {
                std::stringstream res;
                res << "UiElement";
                if (element.mLayer != "")
                    res << "[" << element.mLayer << "]";
                return res.str();
            };
            uiElement["layout"] = sol::property([](const LuaUi::Element& element) { return element.mLayout; },
                [](LuaUi::Element& element, const sol::main_table& layout) { element.mLayout = layout; });
            uiElement["update"] = [luaManager = context.mLuaManager](const std::shared_ptr<LuaUi::Element>& element) {
                if (element->mState != LuaUi::Element::Created)
                    return;
                luaManager->addAction([element] { element->update(); }, "Update UI");
                element->mState = LuaUi::Element::Update;
            };
            uiElement["destroy"] = [luaManager = context.mLuaManager](const std::shared_ptr<LuaUi::Element>& element) {
                if (element->mState == LuaUi::Element::Destroyed)
                    return;
                luaManager->addAction([element] { LuaUi::Element::erase(element.get()); }, "Destroy UI");
                element->mState = LuaUi::Element::Destroy;
            };

            auto uiLayer = context.sol().new_usertype<LuaUi::Layer>("UiLayer");
            uiLayer["name"]
                = sol::readonly_property([](LuaUi::Layer& self) -> std::string_view { return self.name(); });
            uiLayer["size"] = sol::readonly_property([](LuaUi::Layer& self) { return self.size(); });
            uiLayer[sol::meta_function::to_string]
                = [](LuaUi::Layer& self) { return Misc::StringUtils::format("UiLayer(%s)", self.name()); };

            // -- CharacterPreview usertype -----------------------------------------------------------
            auto charPreview = context.sol().new_usertype<LuaCharacterPreview>("CharacterPreview");
            charPreview["textureResource"] = sol::readonly_property(
                [](const LuaCharacterPreview& p) { return p.textureResource(); });
            charPreview["getRotation"] = [](const LuaCharacterPreview& p) { return p.getRotation(); };
            charPreview["getTextureSize"] = [](const LuaCharacterPreview& p) {
                return osg::Vec2f(static_cast<float>(p.getTextureWidth()),
                    static_cast<float>(p.getTextureHeight()));
            };
            charPreview["update"]
                = [luaManager = context.mLuaManager](const std::shared_ptr<LuaCharacterPreview>& p) {
                      luaManager->addAction([p] { p->doUpdate(); }, "CharacterPreview update");
                  };
            charPreview["setActor"] = [luaManager = context.mLuaManager](
                                          const std::shared_ptr<LuaCharacterPreview>& p, const LObject& actor) {
                if (actor.ptr().getType() != ESM::NPC::sRecordId)
                    throw std::runtime_error("setActor: actor must be an NPC");
                MWWorld::Ptr newActor = actor.ptr();
                luaManager->addAction(
                    [p, newActor] { p->doSetActor(newActor); }, "CharacterPreview setActor");
            };
            charPreview["setRotation"] = [luaManager = context.mLuaManager](
                                             const std::shared_ptr<LuaCharacterPreview>& p, float angle) {
                luaManager->addAction(
                    [p, angle] { p->doSetRotation(angle); }, "CharacterPreview setRotation");
            };
            charPreview["destroy"]
                = [luaManager = context.mLuaManager](const std::shared_ptr<LuaCharacterPreview>& p) {
                      luaManager->addAction([p] { p->doDestroy(); }, "CharacterPreview destroy");
                  };

            // -- WorldMap usertype -------------------------------------------------------------------
            auto worldMap = context.sol().new_usertype<LuaWorldMap>("WorldMap");
            worldMap["baseTexture"]
                = sol::readonly_property([](const LuaWorldMap& m) { return m.baseTexture(); });
            worldMap["overlayTexture"]
                = sol::readonly_property([](const LuaWorldMap& m) { return m.overlayTexture(); });
            worldMap["getImageSize"] = [](const LuaWorldMap& m) {
                return osg::Vec2f(static_cast<float>(m.width()), static_cast<float>(m.height()));
            };
            worldMap["worldToImage"] = [](LuaWorldMap& m, float x, float y) { return m.worldToImage(x, y); };
            worldMap["playerArrowAngle"] = [](const LuaWorldMap& m) { return m.playerArrowAngle(); };
            worldMap["markers"] = [](const LuaWorldMap& m, sol::this_state ts) {
                sol::state_view lua(ts);
                sol::table array = lua.create_table();
                int i = 1;
                for (const auto& mk : m.markers())
                {
                    sol::table t = lua.create_table();
                    t["name"] = mk.name;
                    t["x"] = mk.x;
                    t["y"] = mk.y;
                    array[i++] = t;
                }
                return array;
            };
            worldMap["destroy"]
                = [luaManager = context.mLuaManager](const std::shared_ptr<LuaWorldMap>& m) {
                      luaManager->addAction([m] { m->doDestroy(); }, "WorldMap destroy");
                  };

            // -- LocalMap usertype -------------------------------------------------------------------
            auto localMap = context.sol().new_usertype<LuaLocalMap>("LocalMap");
            localMap["isExterior"] = [](const LuaLocalMap& m) { return m.isExterior(); };
            localMap["segments"] = [](LuaLocalMap& m, sol::this_state ts) {
                sol::state_view lua(ts);
                sol::table array = lua.create_table();
                int i = 1;
                for (const auto& segment : m.segments())
                {
                    sol::table entry = lua.create_table();
                    entry["gridX"] = segment.x;
                    entry["gridY"] = segment.y;
                    entry["mapTexture"] = segment.mapTexture;
                    if (segment.fogTexture)
                        entry["fogTexture"] = segment.fogTexture;
                    array[i++] = entry;
                }
                return array;
            };
            localMap["worldToMap"] = [](const LuaLocalMap& m, float x, float y, sol::this_state ts) {
                sol::state_view lua(ts);
                const LuaLocalMap::MapPosition pos = m.worldToMap(x, y);
                sol::table result = lua.create_table();
                result["segX"] = pos.segX;
                result["segY"] = pos.segY;
                result["nx"] = pos.nx;
                result["ny"] = pos.ny;
                return result;
            };
            localMap["playerArrowAngle"] = [](const LuaLocalMap& m) { return m.playerArrowAngle(); };
            localMap["isPositionExplored"]
                = [](const LuaLocalMap& m, int segX, int segY, float nx, float ny) {
                      return m.isPositionExplored(segX, segY, nx, ny);
                  };
            localMap["doorMarkers"] = [](const LuaLocalMap& m, sol::this_state ts) {
                sol::state_view lua(ts);
                sol::table array = lua.create_table();
                int i = 1;
                for (const auto& d : m.doorMarkers())
                {
                    sol::table t = lua.create_table();
                    t["name"] = d.name;
                    t["x"] = d.x;
                    t["y"] = d.y;
                    array[i++] = t;
                }
                return array;
            };
            localMap["destroy"]
                = [luaManager = context.mLuaManager](const std::shared_ptr<LuaLocalMap>& m) {
                      luaManager->addAction([m] { m->doDestroy(); }, "LocalMap destroy");
                  };
        }

        sol::object cached = context.getTypePackage("openmw_ui");
        if (cached != sol::nil)
            return cached;
        else
        {
            sol::table api = LuaUtil::makeReadOnly(registerUiApi(context));
            return context.setTypePackage(api, "openmw_ui");
        }
    }
}
