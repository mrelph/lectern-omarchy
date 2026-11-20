// Theme watching follows Omacut (MIT, David Heinemeier Hansson).
#include "theme.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

namespace {
struct Swatch {
    QColor reader, panel, canvas, chrome, text, muted, accent;
};

struct Palette {
    QString name;
    Swatch light, dark;
};

// The five named palettes use the light and dark interface colors from Lectern's AppLook.
const QList<Palette> palettes{
    {"omarchy", {"#fafafa","#f0f1f3","#e7e9ec","#f5f6f8","#171a21","#505764","#FFD60A"},
                {"#171a21","#1b1e25","#232731","#111318","#edf0f5","#aab2c2","#FFD60A"}},
    {"inkwell", {"#fbf8f1","#e9e2d6","#e8e1d5","#f7f3ec","#1c2b31","#6b6459","#1b6366"},
                {"#171b1d","#202629","#272e31","#111517","#f4eee3","#b9b0a4","#67b5b3"}},
    {"paper", {"#f8f1e3","#ece0c8","#e6d8bd","#f3ead8","#3b2f22","#6a5944","#8a4b1f"},
              {"#221c15","#2b241b","#332a20","#1b1611","#efe3cc","#bfae93","#d9a066"}},
    {"slate", {"#f7f9fb","#e4e9ee","#dde3e9","#f1f4f7","#1d2733","#525f6b","#2f5f8f"},
              {"#161b21","#1f262e","#262e37","#11151a","#e8edf2","#a6b1bd","#7fb0e0"}},
    {"forest", {"#f6f8f2","#e2e8da","#dbe3d1","#f0f4ea","#1e2b1f","#525f4b","#2f6b3a"},
               {"#141a15","#1c241d","#232c24","#0f1410","#e6eedf","#a9b7a2","#7cc08a"}},
    {"midnight", {"#f6f6fb","#e5e5f2","#dedeee","#f0f0f8","#1c1d3a","#545776","#4b4fb0"},
                 {"#12142a","#1a1d38","#212544","#0d0f21","#e9e9f8","#a9abcf","#9aa0ff"}}
};
const QStringList modes{"system", "light", "dark"};

auto findPalette(const QString &name)
{
    return std::find_if(palettes.cbegin(), palettes.cend(), [&](const Palette &p) { return p.name == name; });
}

double luminance(const QColor &color)
{
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) + .0722 * linear(color.blueF());
}

double contrast(QColor a, QColor b)
{
    auto x = luminance(a), y = luminance(b);
    return (std::max(x,y) + .05) / (std::min(x,y) + .05);
}

QColor mix(QColor a, QColor b, double amount)
{
    auto channel = [&](int x, int y) { return qRound(x + (y-x) * amount); };
    return QColor(channel(a.red(), b.red()), channel(a.green(), b.green()), channel(a.blue(), b.blue()));
}

QColor readable(QColor color, QColor toward, const QList<QColor> &surfaces, double floor)
{
    for (int i = 0; i <= 100; ++i) {
        QColor candidate = mix(color, toward, i / 100.);
        if (std::all_of(surfaces.cbegin(), surfaces.cend(),
                        [&](QColor bg) { return contrast(candidate, bg) >= floor; }))
            return candidate;
    }
    return toward;
}

QColor onColor(QColor bg)
{
    return contrast(Qt::black, bg) > contrast(Qt::white, bg) ? Qt::black : Qt::white;
}
}

Theme::Theme(const QString &directory, QObject *parent, const QString &hyprctl)
    : QObject(parent),
      m_directory(directory.isEmpty() ? QDir::homePath() + "/.local/state/omarchy/current" : directory) {
    QSettings settings("Lectern", "lectern-omarchy");
    m_appearance = settings.value("appearance/mode").toString();
    if (!modes.contains(m_appearance)) m_appearance = "system";
    m_palette = settings.value("appearance/palette").toString();
    if (findPalette(m_palette) == palettes.cend()) m_palette = "omarchy";
    connect(qGuiApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this] { updateColors(); });
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);

    // Corners follow Hyprland's effective rounding, which themes and personal
    // overrides can both set, so ask hyprctl rather than reading its config.
    // Without Hyprland the corners stay sharp, like Omarchy's default.
    m_hyprctl = !hyprctl.isEmpty() ? hyprctl
              : directory.isEmpty() && !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")
                  ? QStandardPaths::findExecutable("hyprctl") : QString();
    m_roundingTimeout.setSingleShot(true);
    m_roundingTimeout.setInterval(500);
    connect(&m_roundingTimeout, &QTimer::timeout, &m_roundingProcess, &QProcess::kill);
    connect(&m_roundingProcess, &QProcess::errorOccurred, this, [this] { m_roundingTimeout.stop(); });
    connect(&m_roundingProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_roundingTimeout.stop();
        const auto value = QJsonDocument::fromJson(m_roundingProcess.readAllStandardOutput()).object().value("int");
        if (code != 0 || status != QProcess::NormalExit || !value.isDouble() || value.toInt(-1) < 0)
            return;
        if (value.toInt() != m_radius) {
            m_radius = value.toInt();
            emit changed();
        }
    });
    m_roundingPoll.setInterval(1000);
    connect(&m_roundingPoll, &QTimer::timeout, this, &Theme::refreshRounding);
    if (!m_hyprctl.isEmpty())
        m_roundingPoll.start();
    reload();
}

Theme::~Theme() {
    if (m_roundingProcess.state() != QProcess::NotRunning) {
        m_roundingProcess.kill();
        m_roundingProcess.waitForFinished(1000);
    }
}

void Theme::refreshRounding() {
    if (m_hyprctl.isEmpty() || m_roundingProcess.state() != QProcess::NotRunning)
        return;
    m_roundingProcess.start(m_hyprctl, {"-j", "getoption", "decoration:rounding"});
    m_roundingTimeout.start();
}

void Theme::setAppearance(const QString &mode)
{
    if (mode == m_appearance || !modes.contains(mode)) return;
    m_appearance = mode;
    QSettings("Lectern", "lectern-omarchy").setValue("appearance/mode", mode);
    updateColors(true);
}

void Theme::setInterfacePalette(const QString &name)
{
    if (name == m_palette || findPalette(name) == palettes.cend()) return;
    m_palette = name;
    QSettings("Lectern", "lectern-omarchy").setValue("appearance/palette", name);
    updateColors(true);
}

QVariantMap Theme::paletteAccents() const
{
    QVariantMap accents;
    const QColor desktop(m_desktop.value("accent"));
    const bool dark = m_colors.value("dark").toBool();
    for (const auto &palette : palettes)
        accents.insert(palette.name, palette.name == "omarchy" && desktop.isValid()
            ? desktop : (dark ? palette.dark : palette.light).accent);
    return accents;
}

QString Theme::accentForeground() const
{
    return m_colors.value("onAccent").toString();
}

void Theme::updateColors(bool preferenceChanged)
{
    const auto mode = m_desktop.value("mode");
    const QColor background(m_desktop.value("background"));
    const bool desktopDark = mode == "dark" || (mode != "light" &&
        (background.isValid() ? onColor(background) == Qt::white
                              : qGuiApp->styleHints()->colorScheme() != Qt::ColorScheme::Light));
    const bool dark = m_appearance == "system" ? desktopDark : m_appearance == "dark";
    const auto &palette = *findPalette(m_palette);
    Swatch s = dark ? palette.dark : palette.light;
    QColor selection;
    auto desktopColor = [&](const QString &key, QColor fallback) {
        QColor value(m_desktop.value(key));
        return value.isValid() ? value : fallback;
    };
    if (m_palette == "omarchy") {
        s.accent = desktopColor("accent", s.accent);
        if (desktopDark == dark) {
            s.reader = desktopColor("background", s.reader);
            s.panel = desktopColor("dark_background", s.panel);
            s.chrome = desktopColor("darker_background", s.chrome);
            s.canvas = desktopColor("lighter_background", s.canvas);
            s.text = desktopColor("foreground", s.text);
            s.muted = desktopColor("muted", s.muted);
            selection = desktopColor("selection", s.accent);
        }
    }
    const QColor ink = dark ? Qt::white : Qt::black;
    const QColor paper = dark ? Qt::black : Qt::white;
    // Custom desktop themes can supply tones from either end of the scale.
    for (auto surface : {&s.reader, &s.panel, &s.canvas, &s.chrome})
        *surface = readable(*surface, paper, {ink}, 4.5);
    const QList<QColor> surfaces{s.reader, s.panel, s.canvas, s.chrome};
    const auto text = readable(s.text, ink, surfaces, 4.5);
    const auto focus = readable(s.accent, ink, surfaces, 4.5);
    selection = readable(selection.isValid() ? selection : s.accent, ink, surfaces, 3);
    QVariantMap colors{{"dark",dark}, {"reader",s.reader}, {"panel",s.panel}, {"canvas",s.canvas},
        {"chrome",s.chrome}, {"text",text}, {"muted",readable(s.muted, ink, surfaces, 4.5)},
        {"line",mix(s.panel, text, .30)}, {"accent",s.accent}, {"focus",focus},
        {"onAccent",onColor(s.accent)}, {"selection",selection}, {"onSelection",onColor(selection)}};
    if (preferenceChanged || colors != m_colors) {
        m_colors = colors;
        emit changed();
    }
}

void Theme::reload() {
    refreshRounding();
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);

    // A theme switch replaces symlinks and files, so watch the parents too and
    // re-arm after every change. Watching the nearest existing ancestor means
    // installing Omarchy later works without restarting the app.
    QString ancestor = m_directory;
    while (!QFileInfo::exists(ancestor) && ancestor != "/")
        ancestor = QFileInfo(ancestor).absolutePath();
    QStringList candidates{ancestor, QFileInfo(ancestor).absolutePath(), m_directory,
                           m_directory + "/theme", m_directory + "/theme/colors.toml"};
    candidates.removeDuplicates();
    for (const auto &path : candidates)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);

    m_desktop.clear();
    QFile file(m_directory + "/theme/colors.toml");
    if (file.open(QIODevice::ReadOnly)) {
        static const QRegularExpression entry(R"re(^[ \t]*(\w+)[ \t]*=[ \t]*["'](#[0-9a-fA-F]{6}|light|dark)["'])re",
                                              QRegularExpression::MultilineOption);
        auto matches = entry.globalMatch(QString::fromUtf8(file.readAll()));
        while (matches.hasNext()) {
            const auto match = matches.next();
            m_desktop.insert(match.captured(1), match.captured(2));
        }
    }
    updateColors();
}
