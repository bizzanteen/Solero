#pragma once
#include <QString>
namespace solero {
class NxmRegister {
public:
    static bool isRegistered();                 // xdg-mime query default == solero.desktop
    static bool registerHandler(QString& outMsg); // write desktop file + xdg-mime default + update db
};
}
