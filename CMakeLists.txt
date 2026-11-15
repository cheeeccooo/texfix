cmake_minimum_required(VERSION 3.21)
project(Texfix VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network Concurrent Qml Quick QuickControls2 Pdf Test)
qt_standard_project_setup(REQUIRES 6.8)

# Il motore: nessuna finestra. Lo usano il programma e le prove, chiamandolo direttamente.
qt_add_library(texfix_core STATIC
    src/core/Files.cpp
    src/core/Tools.cpp
    src/core/Project.cpp
    src/core/TexLog.cpp
    src/core/Compiler.cpp
    src/core/SyncTex.cpp
    src/core/History.cpp
    src/core/Settings.cpp
    src/core/Rules.cpp
    src/core/Style.cpp
    src/core/StyleSwitch.cpp
    src/core/Model.cpp
    src/core/Pages.cpp
    src/core/Blocks.cpp
    src/core/Prompts.cpp
    src/core/Convert.cpp
    src/core/Review.cpp
    src/core/Figures.cpp
    src/core/Fix.cpp
    src/core/Layout.cpp
    src/core/AgentTools.cpp
    src/core/Plans.cpp
    src/core/NewLesson.cpp
    src/core/Sort.cpp
    src/core/Git.cpp
    src/core/Reorganize.cpp
)
# Le regole editoriali predefinite entrano nel motore come :/regole/…
file(GLOB_RECURSE RULE_FILES CONFIGURE_DEPENDS RELATIVE ${CMAKE_SOURCE_DIR} regole/*.md)
qt_add_resources(texfix_core "regole" PREFIX "/" FILES ${RULE_FILES})
target_include_directories(texfix_core PUBLIC src)
target_link_libraries(texfix_core PUBLIC Qt6::Core Qt6::Gui)

option(TEXFIX_APP "Compila anche il programma, non solo il motore e le prove" ON)
if(TEXFIX_APP)
# Il programma: una finestra sola in Qt Quick; i tipi C++ di src/app sono il suo modello.
qt_add_executable(texfix MACOSX_BUNDLE
    src/app/main.cpp
)
qt_add_qml_module(texfix
    URI Texfix
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Welcome.qml
        qml/ProjectView.qml
        qml/ProjectPanel.qml
        qml/EditorPane.qml
        qml/TexEditor.qml
        qml/ImageView.qml
        qml/PdfPane.qml
        qml/BottomPanel.qml
        qml/StatusBar.qml
        qml/TxButton.qml
        qml/TxScroll.qml
        qml/TxLabel.qml
        qml/TxSheet.qml
        qml/TxSegmented.qml
        qml/TxTextArea.qml
        qml/SettingsSheet.qml
        qml/PreferencesSheet.qml
        qml/StyleRolesSheet.qml
        qml/ConvertSheet.qml
        qml/OutcomeSheet.qml
        qml/ReviewView.qml
        qml/FiguresView.qml
        qml/LayoutView.qml
        qml/ChatPane.qml
        qml/PlanView.qml
        qml/NewLessonSheet.qml
        qml/GitPanel.qml
        qml/DiffView.qml
        qml/RenameSheet.qml
        qml/OutlineSheet.qml
    SOURCES
        src/app/Theme.h src/app/Theme.cpp
        src/app/Workspace.h src/app/Workspace.cpp
        src/app/EditorBackend.h src/app/EditorBackend.cpp
        src/app/LatexHighlighter.h src/app/LatexHighlighter.cpp
        src/app/LineGutter.h src/app/LineGutter.cpp
        src/app/PdfBackend.h src/app/PdfBackend.cpp
        src/app/Chat.h src/app/Chat.cpp
)
target_include_directories(texfix PRIVATE src src/app)
target_link_libraries(texfix PRIVATE texfix_core Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Pdf)
set_target_properties(texfix PROPERTIES
    OUTPUT_NAME "Texfix"
    MACOSX_BUNDLE_BUNDLE_NAME "Texfix"
    MACOSX_BUNDLE_GUI_IDENTIFIER "it.checco.texfix.nuovo"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)
endif()

# Le prove del motore: senza finestre e senza modello.
enable_testing()
qt_add_executable(texfix_tests tests/tst_core.cpp)
target_link_libraries(texfix_tests PRIVATE texfix_core Qt6::Test)
add_test(NAME core COMMAND texfix_tests)
