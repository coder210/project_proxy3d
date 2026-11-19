using System;
using System.IO;

namespace JoyGame
{
    /// <summary>
    /// 资源根定位：exe 在 &lt;项目根&gt;/build_xxx/Debug/，资源在 &lt;项目根&gt;/assets/。
    /// 从当前目录向上逐级找含 assets/fonts 的目录并 chdir 过去，使所有
    /// assets/...、textures/...、shaders/... 相对路径与 C++ ResourceManager 一致。
    /// </summary>
    internal static class ResourceRoot
    {
        public static bool Align()
        {
            try {
                var dir = new DirectoryInfo(Directory.GetCurrentDirectory());
                for (int i = 0; i < 6 && dir != null; i++, dir = dir.Parent) {
                    if (Directory.Exists(Path.Combine(dir.FullName, "assets", "fonts"))) {
                        Directory.SetCurrentDirectory(dir.FullName);
                        Joy.Host.log("[cs3d] resource root: " + dir.FullName);
                        return true;
                    }
                }
            } catch (Exception ex) {
                Joy.Host.log("[cs3d] resource root failed: " + ex.Message);
            }
            return false;
        }
    }
}
