#include "app.h"

int main(int argc, char** argv) {
    App app;
    // Optional: music folders as CLI args (also available via drag & drop)
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if (arg == "--play") {
            app.SetAutoplay(true);
        } else if (arg == "--shot" && i + 1 < argc) {
            app.SetScreenshotPath(argv[++i]);
        } else if (arg == "--view" && i + 1 < argc) {
            app.SetStartView(argv[++i]);
        } else if (arg == "--import" && i + 1 < argc) {
            // Import an .m3u/.m3u8 playlist on launch (also available via drop)
            app.AddStartupImport(argv[++i]);
        } else {
            app.AddStartupFolder(arg);
        }
    }
    return app.Run();
}
