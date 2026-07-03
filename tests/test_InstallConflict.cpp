#include <QtTest>
#include "install/InstallConflict.h"
#include "core/ModList.h"
using namespace solero;

class TestInstallConflict : public QObject {
    Q_OBJECT
    static ModEntry m(const QString& id, const QString& mid, const QString& fid,
                      const QString& ver) {
        ModEntry e; e.type = EntryType::Mod; e.id = id; e.name = id;
        e.nexusModId = mid; e.nexusFileId = fid; e.version = ver;
        e.stagingFolder = id; return e;
    }
private slots:
    void sameFileWins() {
        ModList ml; ml.append(m("a", "100", "f1", "1.0"));
        auto c = classifyInstallConflict(ml, "100", "f1", "2.0");
        QCOMPARE(c.kind, InstallConflictKind::SameFile);
        QCOMPARE(c.targetEntryId, QString("a"));
    }
    void differentVersionIsVersionConflict() {
        ModList ml; ml.append(m("a", "100", "f1", "1.0"));
        auto c = classifyInstallConflict(ml, "100", "f2", "2.0");
        QCOMPARE(c.kind, InstallConflictKind::VersionConflict);
        QCOMPARE(c.targetEntryId, QString("a"));
    }
    void sameVersionDifferentFileIsSibling() {
        ModList ml; ml.append(m("a", "100", "f1", "1.0"));
        QCOMPARE(classifyInstallConflict(ml, "100", "f2", "1.0").kind,
                 InstallConflictKind::SiblingFile);
    }
    void unknownVersionIsSibling() {
        ModList ml; ml.append(m("a", "100", "f1", "1.0"));
        QCOMPARE(classifyInstallConflict(ml, "100", "f2", "").kind,
                 InstallConflictKind::SiblingFile);
        ModList ml2; ml2.append(m("a", "100", "f1", ""));
        QCOMPARE(classifyInstallConflict(ml2, "100", "f2", "2.0").kind,
                 InstallConflictKind::SiblingFile);
    }
    void normalizedVersionsCompareEqual() {
        // "1.0.0" vs "1.0" style padding must not fake a version conflict -
        // adapt expectations to what normalizeVersion actually does.
        ModList ml; ml.append(m("a", "100", "f1", "2.1.0"));
        QCOMPARE(classifyInstallConflict(ml, "100", "f2", "2.1").kind,
                 InstallConflictKind::SiblingFile);
    }
    void noIdentityNoConflict() {
        ModList ml; ml.append(m("a", "100", "f1", "1.0"));
        QCOMPARE(classifyInstallConflict(ml, "", "", "1.0").kind,
                 InstallConflictKind::NoConflict);
        QCOMPARE(classifyInstallConflict(ml, "999", "f9", "1.0").kind,
                 InstallConflictKind::NoConflict);
    }
    void matchingVariantFileIdIsSameFile() {
        // A fileId that matches a NON-active variant is still "same file":
        // re-installing an already-kept version must not spawn a third copy.
        ModList ml; ml.append(m("a", "100", "f1", "1.0"));
        ml.keepBothAddVariant("a", {"2.0", "f2", "a (2.0)", "", false});
        QCOMPARE(classifyInstallConflict(ml, "100", "f1", "1.0").kind,
                 InstallConflictKind::SameFile);
    }
    void prefersTopLevelEntry() {
        ModList ml;
        ModEntry parent = m("p", "100", "f1", "1.0");
        ModEntry child  = m("c", "100", "f3", "0.5"); child.parentId = "p";
        ml.append(parent); ml.append(child);
        // Two entries share the modId -> grouped multi-file mod: silent SiblingFile
        // targeting the top-level entry, never the version dialog.
        auto c = classifyInstallConflict(ml, "100", "f2", "2.0");
        QCOMPARE(c.kind, InstallConflictKind::SiblingFile);
        QCOMPARE(c.targetEntryId, QString("p"));
    }
    void multipleEntriesSameModIdNeverVersionConflict() {
        // A grouped multi-file mod (DAK main + NG DLL) shares one modId. Installing
        // a different-version + different-file archive must auto-group, not offer
        // Keep Both (which would attach a DLL as a bogus "version" of main).
        ModList ml;
        ModEntry main = m("main", "200", "f1", "1.0");
        ModEntry dll  = m("dll",  "200", "f2", "1.0"); dll.parentId = "main";
        ml.append(main); ml.append(dll);
        auto c = classifyInstallConflict(ml, "200", "f9", "2.0");
        QCOMPARE(c.kind, InstallConflictKind::SiblingFile);
        QCOMPARE(c.targetEntryId, QString("main"));
    }
};
QTEST_GUILESS_MAIN(TestInstallConflict)
#include "test_InstallConflict.moc"
