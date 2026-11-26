/*
 * CraftPad - extensão do Notepad Next no estilo Obsidian.
 * Licença GPL-3.0 (a mesma do Notepad Next).
 */

#include "CraftPadHub.h"
#include "VaultIndex.h"

#include <QCoreApplication>

CraftPadHub::CraftPadHub(QObject *parent) :
    QObject(parent),
    vaultIndex(new VaultIndex(this))
{
}

CraftPadHub *CraftPadHub::instance()
{
    static CraftPadHub *hub = new CraftPadHub(QCoreApplication::instance());
    return hub;
}
