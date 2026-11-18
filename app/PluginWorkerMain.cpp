#include "PluginWorkerRuntime.h"
#include "PluginScanRuntime.h"

#if defined(_WIN32)

class PluginWorkerApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Project Freedom Plugin Worker"; }
    const juce::String getApplicationVersion() override { return "1"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String& commandLine) override
    {
        juce::StringArray arguments;
        arguments.addTokens (commandLine, " \t", "\"");
        if (arguments.size() == 3 && arguments[0] == "--plugin-worker")
        {
            const auto parentId = static_cast<DWORD> (arguments[2].getLargeIntValue());
            runtime = std::make_unique<PluginWorkerRuntime> (arguments[1], parentId);
            if (! runtime->isReady())
                quit();
            return;
        }

        if (arguments.size() == 3 && arguments[0] == "--plugin-scan-worker")
        {
            const auto parentId = static_cast<DWORD> (arguments[2].getLargeIntValue());
            scanRuntime = std::make_unique<PluginScanRuntime> (arguments[1], parentId);
            if (! scanRuntime->isReady())
                quit();
            return;
        }

        quit();
    }

    void shutdown() override
    {
        scanRuntime.reset();
        runtime.reset();
    }
    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<PluginWorkerRuntime> runtime;
    std::unique_ptr<PluginScanRuntime> scanRuntime;
};

START_JUCE_APPLICATION (PluginWorkerApplication)

#else
int main() { return 1; }
#endif
