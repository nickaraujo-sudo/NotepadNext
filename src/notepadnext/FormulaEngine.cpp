/*
 * NotepadNext - recursos próprios deste fork.
 * Licença GPL-3.0 (a mesma do projeto original, dail8859/NotepadNext).
 */

#include "FormulaEngine.h"
#include "MarkdownTable.h"

#include <QDate>
#include <QLocale>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace {

// Argumento de função: um valor único ou um intervalo de células
struct Arg {
    bool isRange = false;
    QVector<FormulaValue> values; // intervalo, linha a linha
    int rows = 0;
    int cols = 0;
    FormulaValue scalar;

    static Arg of(const FormulaValue &v) { Arg a; a.scalar = v; return a; }
    FormulaValue single() const {
        if (!isRange) return scalar;
        if (values.size() == 1) return values.first();
        return FormulaValue::error(QStringLiteral("#VALOR!"));
    }
};

QString normalizeName(const QString &name)
{
    // Remove acentos e pontos para aceitar nomes em português e inglês
    QString s = name.toUpper().normalized(QString::NormalizationForm_D);
    QString out;
    for (QChar ch : s) {
        if (ch.category() == QChar::Mark_NonSpacing)
            continue;
        if (ch == QLatin1Char('.') || ch == QLatin1Char('_'))
            continue;
        out += ch;
    }
    return out;
}

bool toNumber(const FormulaValue &v, double *out)
{
    switch (v.type) {
    case FormulaValue::Number: *out = v.number; return true;
    case FormulaValue::Bool: *out = v.boolean ? 1 : 0; return true;
    case FormulaValue::Empty: *out = 0; return true;
    case FormulaValue::Text: return FormulaEngine::parseNumber(v.text, out);
    default: return false;
    }
}

QString toText(const FormulaValue &v)
{
    return FormulaEngine::format(v);
}

bool toBool(const FormulaValue &v, bool *ok)
{
    *ok = true;
    switch (v.type) {
    case FormulaValue::Bool: return v.boolean;
    case FormulaValue::Number: return v.number != 0;
    case FormulaValue::Empty: return false;
    case FormulaValue::Text: {
        const QString t = normalizeName(v.text);
        if (t == QLatin1String("TRUE") || t == QLatin1String("VERDADEIRO")) return true;
        if (t == QLatin1String("FALSE") || t == QLatin1String("FALSO")) return false;
        double d;
        if (FormulaEngine::parseNumber(v.text, &d)) return d != 0;
        *ok = false;
        return false;
    }
    default: *ok = false; return false;
    }
}

int compareValues(const FormulaValue &a, const FormulaValue &b)
{
    double x, y;
    const bool an = a.type == FormulaValue::Number || a.type == FormulaValue::Bool || a.type == FormulaValue::Empty;
    const bool bn = b.type == FormulaValue::Number || b.type == FormulaValue::Bool || b.type == FormulaValue::Empty;
    if ((an || toNumber(a, &x)) && (bn || toNumber(b, &y))) {
        toNumber(a, &x);
        toNumber(b, &y);
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    return QString::compare(toText(a), toText(b), Qt::CaseInsensitive);
}

// Critério de SOMASE/CONT.SE: ">10", "<>0", "=abc", "abc", "a*"
bool matchesCriteria(const FormulaValue &value, const FormulaValue &criteria)
{
    QString c = toText(criteria);
    QString op = QStringLiteral("=");
    for (const char *candidate : {"<>", ">=", "<=", "=", ">", "<"}) {
        if (c.startsWith(QLatin1String(candidate))) {
            op = QLatin1String(candidate);
            c = c.mid(op.size());
            break;
        }
    }
    FormulaValue target;
    double d;
    if (FormulaEngine::parseNumber(c, &d)) target = FormulaValue::num(d);
    else target = FormulaValue::str(c);

    if (target.type == FormulaValue::Text && (op == QLatin1String("=") || op == QLatin1String("<>")) && (c.contains(QLatin1Char('*')) || c.contains(QLatin1Char('?')))) {
        const QRegularExpression re(QRegularExpression::wildcardToRegularExpression(c), QRegularExpression::CaseInsensitiveOption);
        const bool m = re.match(toText(value)).hasMatch();
        return op == QLatin1String("=") ? m : !m;
    }
    if (target.type == FormulaValue::Number) {
        double v;
        if (!toNumber(value, &v) || value.type == FormulaValue::Empty)
            return op == QLatin1String("<>");
    }
    const int cmp = compareValues(value, target);
    if (op == QLatin1String("=")) return cmp == 0;
    if (op == QLatin1String("<>")) return cmp != 0;
    if (op == QLatin1String(">")) return cmp > 0;
    if (op == QLatin1String("<")) return cmp < 0;
    if (op == QLatin1String(">=")) return cmp >= 0;
    return cmp <= 0;
}

class Parser
{
public:
    Parser(FormulaEngine *engine, const FormulaEngine::TableLookup &lookup, const MarkdownTable *context, const QString &text)
        : engine(engine), lookup(lookup), context(context), s(text)
    {
        // Com ';' separando argumentos, a vírgula é decimal (padrão brasileiro)
        bool inStr = false;
        for (QChar ch : s) {
            if (ch == QLatin1Char('"')) inStr = !inStr;
            if (!inStr && ch == QLatin1Char(';')) { semicolonMode = true; break; }
        }
    }

    FormulaValue run()
    {
        Arg a = parseComparison();
        skipSpaces();
        if (failed.isError()) return failed;
        if (pos < s.size()) return FormulaValue::error(QStringLiteral("#ERRO!"));
        return a.single();
    }

private:
    FormulaEngine *engine;
    const FormulaEngine::TableLookup &lookup;
    const MarkdownTable *context;
    QString s;
    int pos = 0;
    bool semicolonMode = false;
    FormulaValue failed;

    void skipSpaces() { while (pos < s.size() && s.at(pos).isSpace()) ++pos; }
    QChar peek() { skipSpaces(); return pos < s.size() ? s.at(pos) : QChar(); }
    bool accept(const QString &tok) {
        skipSpaces();
        if (s.mid(pos, tok.size()) == tok) { pos += tok.size(); return true; }
        return false;
    }
    void fail(const QString &code) { if (!failed.isError()) failed = FormulaValue::error(code); }

    Arg parseComparison()
    {
        Arg left = parseConcat();
        for (;;) {
            QString op;
            for (const char *candidate : {"<>", ">=", "<=", "=", ">", "<"}) {
                if (accept(QLatin1String(candidate))) { op = QLatin1String(candidate); break; }
            }
            if (op.isEmpty()) return left;
            Arg right = parseConcat();
            const FormulaValue a = left.single(), b = right.single();
            if (a.isError()) return Arg::of(a);
            if (b.isError()) return Arg::of(b);
            const int c = compareValues(a, b);
            bool r = false;
            if (op == QLatin1String("=")) r = c == 0;
            else if (op == QLatin1String("<>")) r = c != 0;
            else if (op == QLatin1String(">")) r = c > 0;
            else if (op == QLatin1String("<")) r = c < 0;
            else if (op == QLatin1String(">=")) r = c >= 0;
            else r = c <= 0;
            left = Arg::of(FormulaValue::logical(r));
        }
    }

    Arg parseConcat()
    {
        Arg left = parseAdditive();
        while (accept(QStringLiteral("&"))) {
            Arg right = parseAdditive();
            const FormulaValue a = left.single(), b = right.single();
            if (a.isError()) return Arg::of(a);
            if (b.isError()) return Arg::of(b);
            left = Arg::of(FormulaValue::str(toText(a) + toText(b)));
        }
        return left;
    }

    Arg arith(const Arg &l, const Arg &r, QChar op)
    {
        const FormulaValue a = l.single(), b = r.single();
        if (a.isError()) return Arg::of(a);
        if (b.isError()) return Arg::of(b);
        double x, y;
        if (!toNumber(a, &x) || !toNumber(b, &y))
            return Arg::of(FormulaValue::error(QStringLiteral("#VALOR!")));
        switch (op.unicode()) {
        case '+': return Arg::of(FormulaValue::num(x + y));
        case '-': return Arg::of(FormulaValue::num(x - y));
        case '*': return Arg::of(FormulaValue::num(x * y));
        case '/':
            if (y == 0) return Arg::of(FormulaValue::error(QStringLiteral("#DIV/0!")));
            return Arg::of(FormulaValue::num(x / y));
        case '^': return Arg::of(FormulaValue::num(std::pow(x, y)));
        }
        return Arg::of(FormulaValue::error(QStringLiteral("#ERRO!")));
    }

    Arg parseAdditive()
    {
        Arg left = parseTerm();
        for (;;) {
            const QChar c = peek();
            if (c == QLatin1Char('+') || c == QLatin1Char('-')) {
                ++pos;
                left = arith(left, parseTerm(), c);
            }
            else return left;
        }
    }

    Arg parseTerm()
    {
        Arg left = parsePower();
        for (;;) {
            const QChar c = peek();
            if (c == QLatin1Char('*') || c == QLatin1Char('/')) {
                ++pos;
                left = arith(left, parsePower(), c);
            }
            else return left;
        }
    }

    Arg parsePower()
    {
        Arg left = parseUnary();
        while (peek() == QLatin1Char('^')) {
            ++pos;
            left = arith(left, parseUnary(), QLatin1Char('^'));
        }
        return left;
    }

    Arg parseUnary()
    {
        const QChar c = peek();
        if (c == QLatin1Char('-')) { ++pos; return arith(Arg::of(FormulaValue::num(0)), parseUnary(), QLatin1Char('-')); }
        if (c == QLatin1Char('+')) { ++pos; return parseUnary(); }
        Arg v = parsePrimary();
        if (peek() == QLatin1Char('%')) {
            ++pos;
            v = arith(v, Arg::of(FormulaValue::num(100)), QLatin1Char('/'));
        }
        return v;
    }

    QString readIdent()
    {
        skipSpaces();
        const int start = pos;
        while (pos < s.size()) {
            const QChar ch = s.at(pos);
            if (ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('.') || ch == QLatin1Char('$'))
                ++pos;
            else break;
        }
        return s.mid(start, pos - start);
    }

    Arg parsePrimary()
    {
        const QChar c = peek();
        if (c.isNull()) { fail(QStringLiteral("#ERRO!")); return Arg(); }

        if (c == QLatin1Char('(')) {
            ++pos;
            Arg v = parseComparison();
            if (!accept(QStringLiteral(")"))) fail(QStringLiteral("#ERRO!"));
            return v;
        }
        if (c == QLatin1Char('"')) {
            ++pos;
            QString str;
            while (pos < s.size()) {
                if (s.at(pos) == QLatin1Char('"')) {
                    if (pos + 1 < s.size() && s.at(pos + 1) == QLatin1Char('"')) { str += QLatin1Char('"'); pos += 2; continue; }
                    break;
                }
                str += s.at(pos++);
            }
            if (pos >= s.size()) { fail(QStringLiteral("#ERRO!")); return Arg(); }
            ++pos;
            return Arg::of(FormulaValue::str(str));
        }
        if (c.isDigit() || c == QLatin1Char('.')) {
            const int start = pos;
            while (pos < s.size() && (s.at(pos).isDigit() || s.at(pos) == QLatin1Char('.')
                                      || (semicolonMode && s.at(pos) == QLatin1Char(',')))) ++pos;
            if (pos < s.size() && (s.at(pos) == QLatin1Char('e') || s.at(pos) == QLatin1Char('E'))
                    && pos + 1 < s.size() && (s.at(pos + 1).isDigit() || s.at(pos + 1) == QLatin1Char('-') || s.at(pos + 1) == QLatin1Char('+'))) {
                pos += 2;
                while (pos < s.size() && s.at(pos).isDigit()) ++pos;
            }
            QString num = s.mid(start, pos - start);
            if (semicolonMode) num.replace(QLatin1Char(','), QLatin1Char('.'));
            bool ok;
            const double d = num.toDouble(&ok);
            if (!ok) { fail(QStringLiteral("#ERRO!")); return Arg(); }
            return Arg::of(FormulaValue::num(d));
        }

        // Referência a outra tabela entre aspas simples: 'Minha tabela'!A1
        QString tableName;
        if (c == QLatin1Char('\'')) {
            ++pos;
            const int end = s.indexOf(QLatin1Char('\''), pos);
            if (end < 0) { fail(QStringLiteral("#ERRO!")); return Arg(); }
            tableName = s.mid(pos, end - pos);
            pos = end + 1;
            if (!accept(QStringLiteral("!"))) { fail(QStringLiteral("#ERRO!")); return Arg(); }
            return parseReference(tableName, readIdent());
        }

        const QString ident = readIdent();
        if (ident.isEmpty()) { fail(QStringLiteral("#ERRO!")); return Arg(); }

        if (pos < s.size() && s.at(pos) == QLatin1Char('!')) {
            ++pos;
            return parseReference(ident, readIdent());
        }
        if (peek() == QLatin1Char('(')) {
            ++pos;
            return callFunction(ident);
        }
        const QString up = normalizeName(ident);
        if (up == QLatin1String("TRUE") || up == QLatin1String("VERDADEIRO")) return Arg::of(FormulaValue::logical(true));
        if (up == QLatin1String("FALSE") || up == QLatin1String("FALSO")) return Arg::of(FormulaValue::logical(false));
        return parseReference(QString(), ident);
    }

    const MarkdownTable *tableFor(const QString &name)
    {
        if (name.isEmpty()) return context;
        if (context && QString::compare(context->meta.name, name, Qt::CaseInsensitive) == 0) return context;
        return lookup ? lookup(name) : nullptr;
    }

    Arg parseReference(const QString &tableName, const QString &first)
    {
        const MarkdownTable *table = tableFor(tableName);
        if (!table) {
            // Tabela não encontrada: lê o resto da referência e devolve #REF!
            if (accept(QStringLiteral(":"))) readIdent();
            return Arg::of(FormulaValue::error(QStringLiteral("#REF!")));
        }

        int r1 = -1, c1 = -1, r2 = -1, c2 = -1;
        const bool firstIsCell = parseCellRef(first, &r1, &c1);
        const int colOnly = firstIsCell ? -1 : columnFromLetters(QString(first).remove(QLatin1Char('$')));

        if (!firstIsCell && colOnly < 0) {
            fail(QStringLiteral("#NOME?"));
            return Arg();
        }

        // Whole column from a cell down: B2# = B2 to the last row of the table.
        // New rows are included automatically; a result cell in the same column is left out.
        if (firstIsCell && pos < s.size() && s.at(pos) == QLatin1Char('#')) {
            ++pos;
            int last = table->rowCount() - 1;
            const FormulaEngine::CurrentCell self = engine->currentCell();
            if (self.table == table && self.col == c1 && self.row >= r1 && self.row <= last)
                last = self.row - 1;
            Arg a;
            a.isRange = true;
            a.cols = 1;
            a.rows = qMax(0, last - r1 + 1);
            for (int r = r1; r <= last; ++r)
                a.values << engine->cellValue(table, r, c1);
            return a;
        }

        if (accept(QStringLiteral(":"))) {
            const QString second = readIdent();
            if (firstIsCell) {
                if (!parseCellRef(second, &r2, &c2)) { fail(QStringLiteral("#REF!")); return Arg(); }
            }
            else {
                // Coluna inteira: B:B (sem o cabeçalho)
                c1 = colOnly;
                c2 = columnFromLetters(QString(second).remove(QLatin1Char('$')));
                if (c2 < 0) { fail(QStringLiteral("#REF!")); return Arg(); }
                r1 = 1;
                r2 = qMax(1, table->rowCount() - 1);
                // A result cell inside the column (e.g. a total at the bottom) is left out
                const FormulaEngine::CurrentCell self = engine->currentCell();
                if (self.table == table && self.col >= c1 && self.col <= c2 && self.row >= r1 && self.row <= r2)
                    r2 = qMax(r1, self.row - 1);
            }
            if (r1 > r2) std::swap(r1, r2);
            if (c1 > c2) std::swap(c1, c2);
            Arg a;
            a.isRange = true;
            a.rows = r2 - r1 + 1;
            a.cols = c2 - c1 + 1;
            for (int r = r1; r <= r2; ++r)
                for (int c = c1; c <= c2; ++c)
                    a.values << engine->cellValue(table, r, c);
            return a;
        }

        if (!firstIsCell) { fail(QStringLiteral("#NOME?")); return Arg(); }
        return Arg::of(engine->cellValue(table, r1, c1));
    }

    QVector<Arg> parseArgs()
    {
        QVector<Arg> args;
        if (accept(QStringLiteral(")"))) return args;
        const QString sep = semicolonMode ? QStringLiteral(";") : QStringLiteral(",");
        for (;;) {
            // Argumento vazio: SE(A1>1;;0)
            if (peek() == sep.at(0) || peek() == QLatin1Char(')')) args << Arg::of(FormulaValue());
            else args << parseComparison();
            if (accept(sep)) continue;
            if (accept(QStringLiteral(")"))) break;
            fail(QStringLiteral("#ERRO!"));
            break;
        }
        return args;
    }

    // Todos os valores (achatando intervalos)
    static QVector<FormulaValue> flatten(const QVector<Arg> &args, int from = 0)
    {
        QVector<FormulaValue> out;
        for (int i = from; i < args.size(); ++i) {
            if (args.at(i).isRange) out += args.at(i).values;
            else out << args.at(i).scalar;
        }
        return out;
    }

    // Números para SOMA/MÉDIA: em intervalos ignora textos; valores diretos são convertidos
    static bool numbers(const QVector<Arg> &args, QVector<double> *out, FormulaValue *err)
    {
        for (const Arg &a : args) {
            if (a.isRange) {
                for (const FormulaValue &v : a.values) {
                    if (v.isError()) { *err = v; return false; }
                    if (v.type == FormulaValue::Number) out->append(v.number);
                    else if (v.type == FormulaValue::Text) {
                        double d;
                        if (FormulaEngine::parseNumber(v.text, &d)) out->append(d);
                    }
                }
            }
            else {
                if (a.scalar.isError()) { *err = a.scalar; return false; }
                if (a.scalar.type == FormulaValue::Empty) continue;
                double d;
                if (!toNumber(a.scalar, &d)) { *err = FormulaValue::error(QStringLiteral("#VALOR!")); return false; }
                out->append(d);
            }
        }
        return true;
    }

    Arg callFunction(const QString &rawName)
    {
        const QString name = normalizeName(rawName);
        const QVector<Arg> args = parseArgs();
        if (failed.isError()) return Arg();
        auto err = [](const char *code) { return Arg::of(FormulaValue::error(QLatin1String(code))); };
        auto num = [](double v) { return Arg::of(FormulaValue::num(v)); };
        auto argNum = [&](int i, double *out) -> bool {
            const FormulaValue v = args.value(i).single();
            return !v.isError() && toNumber(v, out);
        };
        auto firstError = [&]() -> FormulaValue {
            for (const FormulaValue &v : flatten(args)) if (v.isError()) return v;
            return FormulaValue();
        };

        const auto is = [&](std::initializer_list<const char *> names) {
            for (const char *n : names) if (name == QLatin1String(n)) return true;
            return false;
        };

        if (is({"SUM", "SOMA", "AVERAGE", "MEDIA", "MIN", "MINIMO", "MAX", "MAXIMO", "PRODUCT", "MULT", "MEDIAN", "MED"})) {
            QVector<double> v;
            FormulaValue e;
            if (!numbers(args, &v, &e)) return Arg::of(e);
            if (is({"SUM", "SOMA"})) { double t = 0; for (double d : v) t += d; return num(t); }
            if (is({"PRODUCT", "MULT"})) { double t = 1; for (double d : v) t *= d; return num(v.isEmpty() ? 0 : t); }
            if (v.isEmpty()) return is({"MIN", "MINIMO", "MAX", "MAXIMO"}) ? num(0) : err("#DIV/0!");
            if (is({"AVERAGE", "MEDIA"})) { double t = 0; for (double d : v) t += d; return num(t / v.size()); }
            if (is({"MIN", "MINIMO"})) return num(*std::min_element(v.begin(), v.end()));
            if (is({"MAX", "MAXIMO"})) return num(*std::max_element(v.begin(), v.end()));
            std::sort(v.begin(), v.end());
            const int n = v.size();
            return num(n % 2 ? v.at(n / 2) : (v.at(n / 2 - 1) + v.at(n / 2)) / 2);
        }
        if (is({"COUNT", "CONTNUM"})) {
            int n = 0;
            for (const FormulaValue &v : flatten(args)) {
                double d;
                if (v.type == FormulaValue::Number || (v.type == FormulaValue::Text && FormulaEngine::parseNumber(v.text, &d))) ++n;
            }
            return num(n);
        }
        if (is({"COUNTA", "CONTVALORES"})) {
            int n = 0;
            for (const FormulaValue &v : flatten(args)) if (v.type != FormulaValue::Empty) ++n;
            return num(n);
        }
        if (is({"COUNTBLANK", "CONTARVAZIO"})) {
            int n = 0;
            for (const FormulaValue &v : flatten(args)) if (v.type == FormulaValue::Empty || (v.type == FormulaValue::Text && v.text.isEmpty())) ++n;
            return num(n);
        }
        if (is({"IF", "SE"})) {
            if (args.size() < 2) return err("#ERRO!");
            const FormulaValue c = args.at(0).single();
            if (c.isError()) return Arg::of(c);
            bool ok;
            const bool b = toBool(c, &ok);
            if (!ok) return err("#VALOR!");
            if (b) return Arg::of(args.at(1).single());
            return args.size() > 2 ? Arg::of(args.at(2).single()) : Arg::of(FormulaValue::logical(false));
        }
        if (is({"IFERROR", "SEERRO"})) {
            if (args.size() < 2) return err("#ERRO!");
            const FormulaValue v = args.at(0).single();
            return v.isError() ? Arg::of(args.at(1).single()) : Arg::of(v);
        }
        if (is({"AND", "E", "OR", "OU"})) {
            const FormulaValue e = firstError();
            if (e.isError()) return Arg::of(e);
            const bool isAnd = is({"AND", "E"});
            bool result = isAnd;
            for (const FormulaValue &v : flatten(args)) {
                bool ok;
                const bool b = toBool(v, &ok);
                if (!ok) continue;
                result = isAnd ? (result && b) : (result || b);
            }
            return Arg::of(FormulaValue::logical(result));
        }
        if (is({"NOT", "NAO"})) {
            bool ok;
            const bool b = toBool(args.value(0).single(), &ok);
            return ok ? Arg::of(FormulaValue::logical(!b)) : err("#VALOR!");
        }
        if (is({"ROUND", "ARRED", "ROUNDUP", "ARREDONDARPARACIMA", "ROUNDDOWN", "ARREDONDARPARABAIXO"})) {
            double v, d = 0;
            if (!argNum(0, &v) || (args.size() > 1 && !argNum(1, &d))) return err("#VALOR!");
            const double f = std::pow(10.0, int(d));
            double r;
            if (is({"ROUND", "ARRED"})) r = std::round(v * f) / f;
            else if (is({"ROUNDUP", "ARREDONDARPARACIMA"})) r = (v >= 0 ? std::ceil(v * f) : std::floor(v * f)) / f;
            else r = std::trunc(v * f) / f;
            return num(r);
        }
        if (is({"ABS", "SQRT", "RAIZ", "INT", "EXP", "LN", "LOG10", "SIGN", "SINAL", "TRUNC", "TRUNCAR"})) {
            double v;
            if (!argNum(0, &v)) return err("#VALOR!");
            if (is({"ABS"})) return num(std::fabs(v));
            if (is({"SQRT", "RAIZ"})) return v < 0 ? err("#NUM!") : num(std::sqrt(v));
            if (is({"INT"})) return num(std::floor(v));
            if (is({"EXP"})) return num(std::exp(v));
            if (is({"LN"})) return v <= 0 ? err("#NUM!") : num(std::log(v));
            if (is({"LOG10"})) return v <= 0 ? err("#NUM!") : num(std::log10(v));
            if (is({"SIGN", "SINAL"})) return num(v > 0 ? 1 : (v < 0 ? -1 : 0));
            return num(std::trunc(v));
        }
        if (is({"POWER", "POTENCIA", "MOD"})) {
            double a, b;
            if (!argNum(0, &a) || !argNum(1, &b)) return err("#VALOR!");
            if (is({"MOD"})) return b == 0 ? err("#DIV/0!") : num(a - b * std::floor(a / b));
            return num(std::pow(a, b));
        }
        if (is({"PI"})) return num(M_PI);
        if (is({"CONCAT", "CONCATENATE", "CONCATENAR"})) {
            const FormulaValue e = firstError();
            if (e.isError()) return Arg::of(e);
            QString out;
            for (const FormulaValue &v : flatten(args)) out += toText(v);
            return Arg::of(FormulaValue::str(out));
        }
        if (is({"LEN", "NUMCARACT", "UPPER", "MAIUSCULA", "LOWER", "MINUSCULA", "TRIM", "ARRUMAR", "PROPER", "PRI.MAIUSCULA", "PRIMAIUSCULA"})) {
            const FormulaValue v = args.value(0).single();
            if (v.isError()) return Arg::of(v);
            const QString t = toText(v);
            if (is({"LEN", "NUMCARACT"})) return num(t.size());
            if (is({"UPPER", "MAIUSCULA"})) return Arg::of(FormulaValue::str(t.toUpper()));
            if (is({"LOWER", "MINUSCULA"})) return Arg::of(FormulaValue::str(t.toLower()));
            if (is({"TRIM", "ARRUMAR"})) return Arg::of(FormulaValue::str(t.simplified()));
            QString p = t.toLower();
            for (int i = 0; i < p.size(); ++i)
                if (i == 0 || !p.at(i - 1).isLetter()) p[i] = p.at(i).toUpper();
            return Arg::of(FormulaValue::str(p));
        }
        if (is({"LEFT", "ESQUERDA", "RIGHT", "DIREITA"})) {
            const FormulaValue v = args.value(0).single();
            if (v.isError()) return Arg::of(v);
            double n = 1;
            if (args.size() > 1 && !argNum(1, &n)) return err("#VALOR!");
            const QString t = toText(v);
            return Arg::of(FormulaValue::str(is({"LEFT", "ESQUERDA"}) ? t.left(int(n)) : t.right(int(n))));
        }
        if (is({"TODAY", "HOJE"})) return Arg::of(FormulaValue::str(QLocale().toString(QDate::currentDate(), QLocale::ShortFormat)));
        if (is({"SUMIF", "SOMASE", "COUNTIF", "CONTSE", "AVERAGEIF", "MEDIASE"})) {
            if (args.size() < 2) return err("#ERRO!");
            const Arg &range = args.at(0);
            const FormulaValue criteria = args.at(1).single();
            const Arg &sumRange = args.size() > 2 ? args.at(2) : args.at(0);
            const QVector<FormulaValue> rv = range.isRange ? range.values : QVector<FormulaValue>{range.scalar};
            const QVector<FormulaValue> sv = sumRange.isRange ? sumRange.values : QVector<FormulaValue>{sumRange.scalar};
            double total = 0;
            int count = 0;
            for (int i = 0; i < rv.size(); ++i) {
                if (!matchesCriteria(rv.at(i), criteria)) continue;
                ++count;
                double d;
                if (i < sv.size() && toNumber(sv.at(i), &d)) total += d;
            }
            if (is({"COUNTIF", "CONTSE"})) return num(count);
            if (is({"SUMIF", "SOMASE"})) return num(total);
            return count ? num(total / count) : err("#DIV/0!");
        }
        if (is({"VLOOKUP", "PROCV"})) {
            if (args.size() < 3 || !args.at(1).isRange) return err("#VALOR!");
            const FormulaValue key = args.at(0).single();
            double colIndex;
            if (!argNum(2, &colIndex)) return err("#VALOR!");
            bool exact = true;
            if (args.size() > 3) { bool ok; exact = !toBool(args.at(3).single(), &ok); }
            const Arg &t = args.at(1);
            const int c = int(colIndex) - 1;
            if (c < 0 || c >= t.cols) return err("#REF!");
            int found = -1;
            for (int r = 0; r < t.rows; ++r) {
                const int cmp = compareValues(t.values.at(r * t.cols), key);
                if (cmp == 0) { found = r; break; }
                if (!exact && cmp < 0) found = r;
            }
            if (found < 0) return err("#N/D");
            return Arg::of(t.values.at(found * t.cols + c));
        }
        if (is({"INDEX", "INDICE"})) {
            if (args.size() < 2 || !args.at(0).isRange) return err("#VALOR!");
            double r, c = 1;
            if (!argNum(1, &r) || (args.size() > 2 && !argNum(2, &c))) return err("#VALOR!");
            const Arg &t = args.at(0);
            if (t.cols == 1 && args.size() == 2) { c = 1; }
            if (r < 1 || c < 1 || r > t.rows || c > t.cols) return err("#REF!");
            return Arg::of(t.values.at((int(r) - 1) * t.cols + int(c) - 1));
        }
        return err("#NOME?");
    }
};

} // namespace

FormulaEngine::FormulaEngine(TableLookup lookup) : lookup(std::move(lookup))
{
}

bool FormulaEngine::isFormula(const QString &cellText)
{
    const QString t = cellText.trimmed();
    return t.size() > 1 && t.startsWith(QLatin1Char('='));
}

static QString cellKey(const MarkdownTable *table, int row, int col)
{
    return QStringLiteral("%1/%2/%3").arg(quintptr(table)).arg(row).arg(col);
}

FormulaValue FormulaEngine::cellValue(const MarkdownTable *table, int row, int col)
{
    if (!table || row < 0 || col < 0)
        return FormulaValue::error(QStringLiteral("#REF!"));
    if (row >= table->rowCount() || col >= table->columnCount())
        return FormulaValue();

    QString text = table->cell(row, col);
    // Marcações simples de Markdown não atrapalham números: **10** -> 10
    static const QRegularExpression wrap(QStringLiteral("^(\\*\\*|__|\\*|_|~~|`)(.*)\\1$"));
    for (int i = 0; i < 3; ++i) {
        const auto m = wrap.match(text);
        if (!m.hasMatch()) break;
        text = m.captured(2);
    }
    text = text.trimmed();

    if (!isFormula(text)) {
        if (text.isEmpty()) return FormulaValue();
        double d;
        if (parseNumber(text, &d)) return FormulaValue::num(d);
        return FormulaValue::str(text);
    }

    const QString key = cellKey(table, row, col);
    if (cache.contains(key))
        return cache.value(key);
    if (evaluating.contains(key))
        return FormulaValue::error(QStringLiteral("#CICLO!"));

    evaluating.insert(key);
    cellStack.append({table, row, col});
    const FormulaValue v = evaluate(text, table);
    cellStack.removeLast();
    evaluating.remove(key);
    cache.insert(key, v);
    return v;
}

FormulaValue FormulaEngine::evaluate(const QString &formula, const MarkdownTable *context)
{
    QString f = formula.trimmed();
    if (f.startsWith(QLatin1Char('=')))
        f = f.mid(1);
    Parser p(this, lookup, context, f);
    return p.run();
}

QString FormulaEngine::displayValue(const MarkdownTable *table, int row, int col)
{
    const QString raw = table ? table->cell(row, col) : QString();
    if (!isFormula(raw))
        return raw;
    return format(cellValue(table, row, col));
}

QString FormulaEngine::format(const FormulaValue &v)
{
    switch (v.type) {
    case FormulaValue::Empty: return QString();
    case FormulaValue::Text: return v.text;
    case FormulaValue::Error: return v.text;
    case FormulaValue::Bool: return v.boolean ? QStringLiteral("VERDADEIRO") : QStringLiteral("FALSO");
    case FormulaValue::Number: {
        if (std::isnan(v.number) || std::isinf(v.number)) return QStringLiteral("#NUM!");
        QLocale locale;
        locale.setNumberOptions(QLocale::OmitGroupSeparator);
        const double rounded = std::round(v.number * 1e10) / 1e10;
        QString s = locale.toString(rounded, 'f', 10);
        const QChar dp = locale.decimalPoint().at(0);
        if (s.contains(dp)) {
            while (s.endsWith(QLatin1Char('0'))) s.chop(1);
            if (s.endsWith(dp)) s.chop(1);
        }
        if (s == QLatin1String("-0")) s = QStringLiteral("0");
        return s;
    }
    }
    return QString();
}

bool FormulaEngine::parseNumber(const QString &text, double *out)
{
    QString t = text.trimmed();
    if (t.isEmpty()) return false;

    bool percent = false;
    if (t.endsWith(QLatin1Char('%'))) { percent = true; t.chop(1); }
    for (const char *cur : {"R$", "US$", "$", "€", "£"})
        t.remove(QLatin1String(cur));
    t.remove(QLatin1Char(' '));
    t.remove(QChar(0x00A0));
    if (t.isEmpty()) return false;

    static const QRegularExpression valid(QStringLiteral("^[-+]?[0-9.,]*[0-9][0-9.,]*([eE][-+]?[0-9]+)?$"));
    if (!valid.match(t).hasMatch()) return false;

    const int lastDot = t.lastIndexOf(QLatin1Char('.'));
    const int lastComma = t.lastIndexOf(QLatin1Char(','));
    if (lastDot >= 0 && lastComma >= 0) {
        // O separador que aparece por último é o decimal
        if (lastComma > lastDot) { t.remove(QLatin1Char('.')); t.replace(QLatin1Char(','), QLatin1Char('.')); }
        else t.remove(QLatin1Char(','));
    }
    else if (lastComma >= 0) {
        if (t.count(QLatin1Char(',')) > 1) t.remove(QLatin1Char(','));
        else t.replace(QLatin1Char(','), QLatin1Char('.'));
    }
    else if (lastDot >= 0) {
        // "1.234.567" ou, com localidade de vírgula decimal, "1.234" = mil duzentos e trinta e quatro
        static const QRegularExpression thousands(QStringLiteral("^[-+]?\\d{1,3}(\\.\\d{3})+$"));
        const bool commaLocale = QLocale().decimalPoint() == QLatin1String(",");
        if (t.count(QLatin1Char('.')) > 1 || (commaLocale && thousands.match(t).hasMatch()))
            t.remove(QLatin1Char('.'));
    }

    bool ok;
    double d = t.toDouble(&ok);
    if (!ok) return false;
    if (percent) d /= 100.0;
    *out = d;
    return true;
}

QStringList FormulaEngine::functionNames()
{
    return {
        QStringLiteral("SOMA (SUM)"), QStringLiteral("MÉDIA (AVERAGE)"), QStringLiteral("MÍNIMO (MIN)"), QStringLiteral("MÁXIMO (MAX)"),
        QStringLiteral("MULT (PRODUCT)"), QStringLiteral("MED (MEDIAN)"), QStringLiteral("CONT.NÚM (COUNT)"), QStringLiteral("CONT.VALORES (COUNTA)"),
        QStringLiteral("CONTAR.VAZIO (COUNTBLANK)"), QStringLiteral("SE (IF)"), QStringLiteral("SEERRO (IFERROR)"), QStringLiteral("E (AND)"),
        QStringLiteral("OU (OR)"), QStringLiteral("NÃO (NOT)"), QStringLiteral("ARRED (ROUND)"), QStringLiteral("ARREDONDAR.PARA.CIMA (ROUNDUP)"),
        QStringLiteral("ARREDONDAR.PARA.BAIXO (ROUNDDOWN)"), QStringLiteral("ABS"), QStringLiteral("RAIZ (SQRT)"), QStringLiteral("INT"),
        QStringLiteral("TRUNCAR (TRUNC)"), QStringLiteral("POTÊNCIA (POWER)"), QStringLiteral("MOD"), QStringLiteral("EXP"), QStringLiteral("LN"),
        QStringLiteral("LOG10"), QStringLiteral("SINAL (SIGN)"), QStringLiteral("PI"), QStringLiteral("CONCATENAR (CONCAT)"),
        QStringLiteral("NÚM.CARACT (LEN)"), QStringLiteral("MAIÚSCULA (UPPER)"), QStringLiteral("MINÚSCULA (LOWER)"), QStringLiteral("ARRUMAR (TRIM)"),
        QStringLiteral("PRI.MAIÚSCULA (PROPER)"), QStringLiteral("ESQUERDA (LEFT)"), QStringLiteral("DIREITA (RIGHT)"), QStringLiteral("HOJE (TODAY)"),
        QStringLiteral("SOMASE (SUMIF)"), QStringLiteral("CONT.SE (COUNTIF)"), QStringLiteral("MÉDIASE (AVERAGEIF)"), QStringLiteral("PROCV (VLOOKUP)"),
        QStringLiteral("ÍNDICE (INDEX)"),
    };
}
