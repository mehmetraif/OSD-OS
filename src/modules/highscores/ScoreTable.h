#pragma once
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// A game's high scores as the High Scores module keeps and shows them, read
// from MAME's files by openhi2txt (HighScoresBackend), with no file or
// library of its own, so tests/highscores_test.cpp can drive it.
namespace scores {

// One of a game's tables: most have one, some several (by difficulty, by
// player, a table of times beside the scores).
struct Table {
    QString id;                  // the definition's name for it: "TOP SCORES"; "" for none
    QStringList columns;         // "RANK", "SCORE", "NAME"
    QList<QStringList> rows;     // a cell for each column
};

struct Game {
    QString set;                 // its short name: "ffight"
    // What MAME kept them in: "ffight.hi" (the hiscore plugin's) or the
    // file of the game's NVRAM read, "nvram/tgm2/eeprom".
    QString file;
    // What the game's files were when it was read, their names, sizes and
    // times: any change, a file new or gone, reads it again.
    QString stamp;
    QList<Table> tables;
    QList<QPair<QString, QString>> fields;   // "TOP SCORE" → "116,370"
    // Why the file couldn't be read; "" when it was. Tables and fields are
    // empty then.
    QString error;
    // A game whose NVRAM has no table hi2txt knows of: most keep only
    // their settings there. Kept, so it isn't looked at again until it
    // changes, but not shown.
    bool hidden = false;

    QVariantMap toVariant() const;
    static Game fromVariant(const QVariantMap &map);
};

// A table's rows as lines of text whose columns line up in a monospaced font
// (the OSD's): numbers to the right of theirs, the rest to the left, two
// spaces between.
QStringList lines(const Table &table);
// A column's or a table's name as the definition gives it, its underscores
// as spaces: "TODAYS_CHARACTERS_3" → "TODAYS CHARACTERS 3". Its case is
// kept: most are in capitals, as the OSD writes, and "VS CPU MODE" reads
// wrong as anything else.
QString label(const QString &id);
// What a row holds, column by column: "RANK 1", "SCORE 116,370",
// "NAME ACD"; the cells left blank are left out.
QStringList describeRow(const Table &table, int row);

} // namespace scores
