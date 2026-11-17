#ifndef NOMINMAX
 #define NOMINMAX
#endif

#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"
#include "Theme.h"
#include "WindowPlacement.h"
#include <limits>

#if defined(_WIN32)
 #include <windows.h>
 #include <shlobj_core.h>
#endif

namespace
{
roll::Rect toRollRect (juce::Rectangle<int> bounds)
{
    return { bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight() };
}

juce::Rectangle<int> toJuceRect (const roll::Rect& bounds)
{
    return { bounds.x, bounds.y, bounds.width, bounds.height };
}

std::vector<roll::Rect> getDisplayUserAreas()
{
    std::vector<roll::Rect> areas;
    const auto& displays = juce::Desktop::getInstance().getDisplays().displays;
    areas.reserve (static_cast<std::size_t> (displays.size()));

    for (const auto& display : displays)
        areas.push_back (toRollRect (display.userBounds.toNearestInt()));

    return areas;
}

#if defined(_WIN32)
bool registerProjectFileAssociation (const juce::String& extension,
                                     const juce::String& programId,
                                     const juce::String& description,
                                     const juce::File& executable)
{
    const bool registered = juce::WindowsRegistry::registerFileAssociation (
        extension, programId, description, executable, 0, true);
    if (! registered)
        return false;

    const auto command = "\"" + executable.getFullPathName() + "\" \"%1\"";
    return juce::WindowsRegistry::setValue (
        "HKEY_CURRENT_USER\\Software\\Classes\\" + programId + "\\shell\\open\\command",
        command);
}
#endif
}

class FreedomApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }

    void initialise (const juce::String& commandLine) override
    {
#if defined(_WIN32)
        const auto executable = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
        const bool pfsRegistered = registerProjectFileAssociation (
            ".pfs", "ProjectFreedomProject", "Project Freedom Session", executable);
        const bool legacyRegistered = registerProjectFileAssociation (
            ".freedom", "ProjectFreedomLegacyProject", "Project Freedom Legacy Session", executable);
        if (! pfsRegistered || ! legacyRegistered)
            juce::Logger::writeToLog ("ProjectFreedom: nao foi possivel registrar as associacoes de projeto.");
        else
            SHChangeNotify (SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
#endif

        if (auto* laf = dynamic_cast<juce::LookAndFeel_V4*> (&juce::LookAndFeel::getDefaultLookAndFeel()))
        {
            laf->setColourScheme (juce::LookAndFeel_V4::getMidnightColourScheme());
            laf->setColour (juce::ResizableWindow::backgroundColourId, theme::background());
            laf->setColour (juce::TextButton::buttonColourId, theme::control());
            laf->setColour (juce::TextButton::buttonOnColourId, theme::accent());
            laf->setColour (juce::TextButton::textColourOffId, theme::text());
            laf->setColour (juce::TextButton::textColourOnId, theme::text());
            laf->setColour (juce::ComboBox::backgroundColourId, theme::control());
            laf->setColour (juce::ComboBox::textColourId, theme::text());
            laf->setColour (juce::ComboBox::outlineColourId, theme::border());
            laf->setColour (juce::TextEditor::backgroundColourId, theme::control());
            laf->setColour (juce::TextEditor::textColourId, theme::text());
            laf->setColour (juce::TextEditor::outlineColourId, theme::border());
            laf->setColour (juce::TextEditor::focusedOutlineColourId, theme::accent());
            laf->setColour (juce::PopupMenu::backgroundColourId, theme::panel());
            laf->setColour (juce::PopupMenu::textColourId, theme::text());
            laf->setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::accent().withAlpha (0.25f));
            laf->setColour (juce::PopupMenu::highlightedTextColourId, theme::text());
            laf->setColour (juce::Slider::thumbColourId, theme::text());
            laf->setColour (juce::Slider::trackColourId, theme::accent());
        }
        window = std::make_unique<MainWindow> (getApplicationName());
        window->offerRecoveryBeforeOpening ([this, commandLine]
        {
            openProjectArgument (commandLine);
        });
    }
    void shutdown() override                       { window.reset(); }
    void systemRequestedQuit() override
    {
        if (window != nullptr)
            window->requestClose();
        else
            quit();
    }
    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        openProjectArgument (commandLine);
    }

private:
    void openProjectArgument (const juce::String& commandLine)
    {
        juce::StringArray arguments;
        arguments.addTokens (commandLine, " \t", "\"");
        for (const auto& argument : arguments)
        {
            const juce::File file (argument.unquoted());
            const auto extension = file.getFileExtension();
            if (extension.equalsIgnoreCase (".pfs") || extension.equalsIgnoreCase (".freedom"))
            {
                if (window != nullptr)
                    window->openProject (file);
                return;
            }
        }
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, theme::background(), DocumentWindow::allButtons)
        {
            juce::PropertiesFile::Options options;
            options.applicationName = name;
            options.filenameSuffix = ".settings";
            options.folderName = "ProjectFreedom";
            options.osxLibrarySubFolder = "Application Support";
            appProperties.setStorageParameters (options);
            userSettings = appProperties.getUserSettings();

            setUsingNativeTitleBar (true);
            setResizable (true, true);
            mainComponent = new MainComponent();
            setContentOwned (mainComponent, true);
            mainComponent->setMainWindow (this);
            restoreSavedBounds();
            setVisible (true);
            if (maximizeWhenShown)
                maximizeAtStartup();
        }

        ~MainWindow() override
        {
            saveWindowState();
            appProperties.saveIfNeeded();
        }

        void openProject (const juce::File& file)
        {
            if (mainComponent != nullptr)
                mainComponent->openProjectFromFile (file);
        }

        void offerRecoveryBeforeOpening (std::function<void()> continueStartup)
        {
            if (mainComponent != nullptr)
                mainComponent->offerRecoveryBeforeOpening (std::move (continueStartup));
            else if (continueStartup)
                continueStartup();
        }

        void closeButtonPressed() override
        {
            saveWindowState();
            requestClose();
        }

        void requestClose()
        {
            juce::Component::SafePointer<MainWindow> safeThis (this);
            if (mainComponent == nullptr)
            {
                juce::JUCEApplication::getInstance()->quit();
                return;
            }

            mainComponent->requestClose ([safeThis]
            {
                if (safeThis != nullptr)
                    juce::JUCEApplication::getInstance()->quit();
            });
        }

        void activeWindowStatusChanged() override
        {
            juce::DocumentWindow::activeWindowStatusChanged();
            if (mainComponent != nullptr)
                mainComponent->updatePluginWindowZOrder();
        }

        void moved() override
        {
            juce::DocumentWindow::moved();
            updateWindowState();
        }

        void resized() override
        {
            juce::DocumentWindow::resized();
            updateWindowState();
        }

    private:
        juce::ApplicationProperties appProperties;
        juce::PropertiesFile* userSettings = nullptr;
        MainComponent* mainComponent = nullptr;
        roll::Rect normalBounds;
        bool maximizeWhenShown = false;
        bool updatingResizeLimits = false;

        juce::Rectangle<int> primaryUserArea() const
        {
            if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                return display->userBounds.toNearestInt();

            return { 0, 0, 1280, 760 };
        }

        const juce::Displays::Display* displayForWindow() const
        {
            const auto& displays = juce::Desktop::getInstance().getDisplays();
            if (const auto* display = displays.getDisplayForRect (getBounds()))
                return display;

            return displays.getPrimaryDisplay();
        }

        bool isNativeMaximized() const
        {
           #if defined(_WIN32)
            if (auto* peer = getPeer())
                return IsZoomed (static_cast<HWND> (peer->getNativeHandle())) != FALSE;
           #endif
            return false;
        }

        void maximizeAtStartup()
        {
           #if defined(_WIN32)
            if (auto* peer = getPeer())
                ShowWindow (static_cast<HWND> (peer->getNativeHandle()), SW_MAXIMIZE);
           #else
            if (const auto* display = displayForWindow())
                setBounds (display->userBounds.toNearestInt());
           #endif
        }

        void updateResizeLimits()
        {
            if (updatingResizeLimits)
                return;

            const auto* display = displayForWindow();
            if (display == nullptr)
                return;

            const auto area = display->userBounds.toNearestInt();
            updatingResizeLimits = true;
            setResizeLimits (std::min (1000, area.getWidth()),
                             std::min (620, area.getHeight()),
                             std::numeric_limits<int>::max(),
                             std::numeric_limits<int>::max());
            updatingResizeLimits = false;
        }

        void updateWindowState()
        {
            updateResizeLimits();
            if (! isNativeMaximized() && ! isFullScreen() && ! getBounds().isEmpty())
                normalBounds = toRollRect (getBounds());
        }

        void restoreSavedBounds()
        {
            const auto primary = toRollRect (primaryUserArea());
            normalBounds = roll::WindowPlacement::defaultMainWindowBounds (primary);

            if (userSettings != nullptr)
            {
                juce::StringArray values;
                values.addTokens (userSettings->getValue ("windowBounds"), " ,;", "");
                values.trim();
                values.removeEmptyStrings();

                if (values.size() == 4)
                {
                    const roll::Rect saved { values[0].getIntValue(), values[1].getIntValue(),
                                             values[2].getIntValue(), values[3].getIntValue() };
                    if (saved.width > 0 && saved.height > 0)
                    {
                        normalBounds = roll::WindowPlacement::clampToActiveDisplays (
                            saved, getDisplayUserAreas(), primary);
                        maximizeWhenShown = userSettings->getBoolValue ("windowMaximized", false);
                    }
                }
            }

            setBounds (toJuceRect (normalBounds));
            updateResizeLimits();
        }

        void saveWindowState()
        {
            if (userSettings == nullptr)
                return;

            if (! isNativeMaximized() && ! isFullScreen() && ! getBounds().isEmpty())
                normalBounds = toRollRect (getBounds());

            userSettings->setValue ("windowBounds",
                                    juce::String (normalBounds.x) + " "
                                    + juce::String (normalBounds.y) + " "
                                    + juce::String (normalBounds.width) + " "
                                    + juce::String (normalBounds.height));
            userSettings->setValue ("windowMaximized", isNativeMaximized());
        }
    };

    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (FreedomApplication)
