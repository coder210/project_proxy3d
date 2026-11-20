using System;

namespace JoyGame
{
    /// <summary>C# 场景管理器（对齐 C++ SceneManager：change 立即切换，pending 延时切换）。</summary>
    public sealed class SceneManager3D
    {
        private readonly GameContext _ctx;
        private IScene3D _current;
        private IScene3D _pending;

        public IScene3D Current => _current;
        public string CurrentName => _current != null ? _current.Name : "(none)";

        public SceneManager3D(GameContext ctx) { _ctx = ctx; }

        public void Change(IScene3D next)
        {
            if (_current != null) _current.Destroy();
            _current = next;
            _pending = null;
            if (next != null) next.Enter();
        }

        public void SetPending(IScene3D next) { _pending = next; }

        public void ProcessPending()
        {
            if (_pending == null) return;
            Change(_pending);
        }

        public void Update(float dt) { if (_current != null) _current.Update(dt); }
        public void FixedUpdate(float dt)
        {
            var fixedScene = _current as IFixedUpdateScene;
            if (fixedScene != null) fixedScene.FixedUpdate(dt);
        }
        public void Render() { if (_current != null) _current.Render(); }
        public void OnEvent(UserEvent e) { if (_current != null) _current.OnEvent(e); }

        public void QuitCurrent() { if (_current != null) _current.Destroy(); _current = null; }
    }
}
