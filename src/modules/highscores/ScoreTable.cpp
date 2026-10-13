#include "ScoreTable.h"
#include <QRegularExpression>
#include <QVariantList>
#include <algorithm>

namespace scores {

namespace {

// A cell that reads as a number: a score with its grouping commas, a
// percentage, a time's digits.
bool isNumber(const QString &cell) {
    static const QRegularExpression number(QStringLiteral("^[-+]?[0-9][0-9,.:]*%?$"));
    return number.match(cell).hasMatch();
}

} // namespace

QVariantMap Game::toVariant() const {
    QVariantList tableList;
    for (const Table &table : tables) {
        QVariantList rowList;
        for (const QStringList &row : table.rows)
            rowList << QVariant(row);
        tableList << QVariantMap{ { QStringLiteral("id"), table.id },
                                  { QStringLiteral("columns"), table.columns },
                                  { QStringLiteral("rows"), rowList } };
    }
    QVariantList fieldList;
    for (const auto &[id, value] : fields)
        fieldList << QVariant(QVariantList{ id, value });
    // Only what a game has, to keep the file short.
    QVariantMap map{ { QStringLiteral("file"), file }, { QStringLiteral("stamp"), stamp } };
    if (!tableList.isEmpty())
        map.insert(QStringLiteral("tables"), tableList);
    if (!fieldList.isEmpty())
        map.insert(QStringLiteral("fields"), fieldList);
    if (!error.isEmpty())
        map.insert(QStringLiteral("error"), error);
    if (hidden)
        map.insert(QStringLiteral("hidden"), true);
    return map;
}

Game Game::fromVariant(const QVariantMap &map) {
    Game game;
    game.file = map.value(QStringLiteral("file")).toString();
    game.stamp = map.value(QStringLiteral("stamp")).toString();
    const QVariantList tableList = map.value(QStringLiteral("tables")).toList();
    for (const QVariant &v : tableList) {
        const QVariantMap t = v.toMap();
        Table table;
        table.id = t.value(QStringLiteral("id")).toString();
        table.columns = t.value(QStringLiteral("columns")).toStringList();
        const QVariantList rowList = t.value(QStringLiteral("rows")).toList();
        for (const QVariant &row : rowList)
            table.rows << row.toStringList();
        game.tables << table;
    }
    const QVariantList fieldList = map.value(QStringLiteral("fields")).toList();
    for (const QVariant &v : fieldList) {
        const QVariantList field = v.toList();
        if (field.size() == 2)
            game.fields << qMakePair(field.at(0).toString(), field.at(1).toString());
    }
    game.error = map.value(QStringLiteral("error")).toString();
    game.hidden = map.value(QStringLiteral("hidden")).toBool();
    return game;
}

QStringList lines(const Table &table) {
    const int columns = int(table.columns.size());
    const auto at = [](const QStringList &row, int c) { return c < row.size() ? row.at(c) : QString(); };
    QList<bool> numeric(columns, true);
    for (const QStringList &row : table.rows) {
        for (int c = 0; c < columns; ++c) {
            const QString cell = at(row, c).trimmed();
            if (!cell.isEmpty() && !isNumber(cell))
                numeric[c] = false;
        }
    }
    // A number without the spaces around it, other text without those after
    // it: a space before a name is one of its letters, as the game showed it.
    const auto shown = [&](const QStringList &row, int c) {
        QString cell = at(row, c);
        if (numeric[c])
            return cell.trimmed();
        while (cell.endsWith(QLatin1Char(' ')))
            cell.chop(1);
        return cell;
    };
    QList<int> width(columns, 0);
    for (const QStringList &row : table.rows) {
        for (int c = 0; c < columns; ++c)
            width[c] = std::max(width[c], int(shown(row, c).size()));
    }
    QStringList out;
    for (const QStringList &row : table.rows) {
        QStringList cells;
        for (int c = 0; c < columns; ++c) {
            // A column blank in every row takes no room.
            if (width[c] == 0)
                continue;
            const QString cell = shown(row, c);
            cells << (numeric[c] ? cell.rightJustified(width[c]) : cell.leftJustified(width[c]));
        }
        QString line = cells.join(QStringLiteral("  "));
        // The last column's padding lines nothing up.
        while (line.endsWith(QLatin1Char(' ')))
            line.chop(1);
        out << line;
    }
    return out;
}

QString label(const QString &id) {
    return QString(id).replace(QLatin1Char('_'), QLatin1Char(' ')).simplified();
}

QStringList describeRow(const Table &table, int row) {
    QStringList parts;
    if (row < 0 || row >= table.rows.size())
        return parts;
    const QStringList &cells = table.rows.at(row);
    for (int c = 0; c < table.columns.size() && c < cells.size(); ++c) {
        const QString cell = cells.at(c).trimmed();
        if (!cell.isEmpty())
            parts << label(table.columns.at(c)) + QLatin1Char(' ') + cell;
    }
    return parts;
}

} // namespace scores
