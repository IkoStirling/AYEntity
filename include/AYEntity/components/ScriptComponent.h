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
        if (_bridge && !_scriptName.empty()) {
            _bridge->call("onStart", this, entity);
        }
    }

    void onUpdate(float dt) override {
        if (_bridge && !_scriptName.empty()) {
            _bridge->call("onUpdate", this, &dt);
        }
    }

    void onDetach() override {
        if (_bridge && !_scriptName.empty()) {
            _bridge->call("onDestroy", this);
        }
        _entity = nullptr;
    }

    void onStart() override {
        if (_bridge && !_scriptName.empty()) {
            _bridge->call("onStart", this, _entity);
        }
    }

    // ===== 属性 =====
    const char* getScriptName() const { return _scriptName.c_str(); }
    void setScriptName(const char* name) { _scriptName = name ? name : ""; }

    Entity* getEntity() const { return _entity; }

    // ===== 桥接设置 =====
    // Legacy non-owning binding. Callers must keep `bridge` alive for the
    // component lifetime. Production script hosts should prefer the shared
    // overload below so subsystem teardown cannot leave a dangling pointer.
    void setBridge(IScriptBridge* bridge) {
        _ownedBridge.reset();
        _bridge = bridge;
    }

    // Owning binding used by AYScript. The adapter implementation may outlive
    // ScriptSubSystem, while the adapter itself is detached from the runtime
    // during shutdown and becomes a safe no-op.
    void setBridge(std::shared_ptr<IScriptBridge> bridge) {
        _ownedBridge = std::move(bridge);
        _bridge = _ownedBridge.get();
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
};

} // namespace ayt::entity
