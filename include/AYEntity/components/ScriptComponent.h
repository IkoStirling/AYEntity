#pragma once
// AYEntity/components/AYEntity/components/AYEntity/components/AYEntity/components/ScriptComponent.h - 脚本组件

#include <AYEntity/IEntity.h>
#include <memory>
#include <string>

namespace ayt::entity
{

// =============================================================================
// IScriptBridge - 脚本桥接接口（AYScript 未实现前使用空接口）
// =============================================================================
class IScriptBridge {
public:
    virtual ~IScriptBridge() = default;
    virtual bool call(const char* method, void* arg1 = nullptr, void* arg2 = nullptr) { return false; }
    virtual bool hasScript(const char* scriptName) const { return false; }
};

// =============================================================================
// ScriptComponent - 脚本组件
// =============================================================================
class ScriptComponent : public IComponent {
public:
    const char* getName() const override {
        return _scriptName.empty() ? "ScriptComponent" : _scriptName.c_str();
    }

    void onAttach(Entity* entity) override {
        _entity = entity;
        activateScript();
    }

    void onUpdate(float dt) override {
        activateScript();
        if (_started && _bridge) {
            _bridge->call("onUpdate", this, &dt);
        }
    }

    void onDetach() override {
        if (_started && _bridge) {
            _started = false;
            _bridge->call("onDestroy", this);
        }
        _started = false;
        _entity = nullptr;
    }

    void onStart() override { activateScript(); }

    // Component attachment may precede script compilation and bridge binding.
    // Repeated activation is safe; onStart runs once per attached binding.
    // A missing on_start handler still counts as started once the bridge has
    // loaded this script, so on_destroy is dispatched on detach.
    void activateScript() {
        if (_started || !_entity || !_bridge || _scriptName.empty()) return;
        if (!_bridge->hasScript(_scriptName.c_str())) return;
        _started = true;
        _bridge->call("onStart", this, _entity);
    }

    // ===== 属性 =====
    const char* getScriptName() const { return _scriptName.c_str(); }
    void setScriptName(const char* name) {
        const std::string next = name ? name : "";
        if (_scriptName == next) return;
        _scriptName = next;
        _started = false;
        activateScript();
    }

    Entity* getEntity() const { return _entity; }

    // ===== 桥接设置 =====
    // Legacy non-owning binding. Callers must keep `bridge` alive for the
    // component lifetime. Production script hosts should prefer the shared
    // overload below so subsystem teardown cannot leave a dangling pointer.
    void setBridge(IScriptBridge* bridge) {
        if (_bridge == bridge) return;
        _ownedBridge.reset();
        _bridge = bridge;
        _started = false;
        activateScript();
    }

    // Owning binding used by AYScript. The adapter implementation may outlive
    // ScriptSubSystem, while the adapter itself is detached from the runtime
    // during shutdown and becomes a safe no-op.
    void setBridge(std::shared_ptr<IScriptBridge> bridge) {
        if (_bridge == bridge.get()) return;
        _ownedBridge = std::move(bridge);
        _bridge = _ownedBridge.get();
        _started = false;
        activateScript();
    }
    IScriptBridge* getBridge() const { return _bridge; }

    // ===== 脚本方法调用 =====
    bool callScriptMethod(const char* method) {
        if (!_bridge || _scriptName.empty()) return false;
        return _bridge->call(method, this);
    }

private:
    std::string _scriptName;
    Entity* _entity = nullptr;
    std::shared_ptr<IScriptBridge> _ownedBridge;
    IScriptBridge* _bridge = nullptr;
    bool _started = false;
};

} // namespace ayt::entity
