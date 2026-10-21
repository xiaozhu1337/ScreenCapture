#pragma once
#include <KGlobalAccel>
#include <KGlobalShortcutInfo>
#include <QCoreApplication>

inline bool shortcutAvailable(const QKeySequence& key)
{
    if (key.isEmpty() || KGlobalAccel::isGlobalShortcutAvailable(key)) return true;
    const auto owners = KGlobalAccel::globalShortcutsByKey(key);
    if (owners.isEmpty()) return false;
    for (const auto& owner : owners)
        if (owner.componentUniqueName() != QCoreApplication::applicationName()
            || (owner.uniqueName() != "capture" && owner.uniqueName() != "ocr")) return false;
    return true;
}
