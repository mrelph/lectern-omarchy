#pragma once

#include <QJsonObject>
#include <QStringList>

namespace AgentCommands
{
bool handles(const QString &command);
int run(const QStringList &arguments);
int serve();
QJsonObject perform(const QString &tool, const QJsonObject &arguments, bool full = true);
}
