/*
 * CraftPad - extensão do Notepad Next no estilo Obsidian.
 * Licença GPL-3.0 (a mesma do Notepad Next).
 */

#include "GraphView.h"
#include "CraftPadHub.h"
#include "VaultIndex.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QRadialGradient>
#include <QTimer>
#include <QtMath>

// ---- Ajustes da física -------------------------------------------------
static constexpr double Repulsion = 2600.0;   // empurrão entre todos os nós
static constexpr double SpringLength = 80.0;  // distância ideal de uma ligação
static constexpr double SpringStrength = 0.045;
static constexpr double Gravity = 0.010;      // puxão para o centro
static constexpr double Damping = 0.82;       // atrito
static constexpr double MaxSpeed = 14.0;
static constexpr double MinAlpha = 0.035;     // nunca para totalmente: a galáxia "respira"

// ---- Rotação quando ocioso --------------------------------------------
static constexpr qint64 IdleDelayMs = 3500;
static constexpr double IdleSpin = 0.00055;   // rad por quadro (~1 volta a cada 3 min)

// Cores vibrantes por pasta (notas na raiz usam a primeira)
static const QVector<QColor> &folderPalette()
{
    static const QVector<QColor> colors = {
        QColor(0xB3, 0x88, 0xFF), // violeta
        QColor(0x22, 0xD3, 0xEE), // ciano
        QColor(0xA3, 0xE6, 0x35), // lima
        QColor(0xFB, 0xBF, 0x24), // âmbar
        QColor(0x38, 0xBD, 0xF8), // azul céu
        QColor(0xFB, 0x92, 0x3C), // laranja
        QColor(0x34, 0xD3, 0x99), // esmeralda
        QColor(0xE8, 0x79, 0xF9), // fúcsia
    };
    return colors;
}

GraphView::GraphView(VaultIndex *index, QWidget *parent) :
    QWidget(parent),
    index(index),
    timer(new QTimer(this))
{
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(320, 240);

    timer->setInterval(16);
    timer->setTimerType(Qt::PreciseTimer);
    connect(timer, &QTimer::timeout, this, &GraphView::step);
    clock.start();

    auto *rng = QRandomGenerator::global();
    for (int i = 0; i < 260; ++i) {
        stars.append({rng->generateDouble() * 2 * M_PI,
                      qSqrt(rng->generateDouble()) * 1400.0,
                      0.4 + rng->generateDouble() * 1.4,
                      rng->generateDouble() * 2 * M_PI});
    }

    connect(index, &VaultIndex::indexChanged, this, &GraphView::rebuild);
    rebuild();
}

void GraphView::setShowTags(bool show) { showTags = show; rebuild(); }
void GraphView::setShowOrphans(bool show) { showOrphans = show; rebuild(); }
void GraphView::setIdleRotation(bool on) { idleRotation = on; }

void GraphView::setCurrentFile(const QString &path)
{
    currentFile = path;
    update();
}

void GraphView::fitToView()
{
    autoFit = true;
    touch();
}

void GraphView::rebuild()
{
    // Guarda as posições atuais para a galáxia não "pular" ao atualizar
    QHash<QString, QPair<QPointF, QPointF>> oldState;
    for (const Node &n : std::as_const(nodes))
        oldState.insert(n.id, {n.pos, n.vel});

    nodes.clear();
    edges.clear();
    QHash<QString, int> ids;

    auto addNode = [&](const QString &id, const QString &label, const QString &payload, Kind kind, const QColor &color) {
        if (ids.contains(id))
            return ids.value(id);
        Node n;
        n.id = id;
        n.label = label;
        n.payload = payload;
        n.kind = kind;
        n.color = color;
        ids.insert(id, nodes.size());
        nodes.append(n);
        return int(nodes.size() - 1);
    };

    // Uma cor por pasta de primeiro nível
    QStringList groups;
    const QDir root(index->root());
    auto groupColor = [&](const QString &path) {
        const QString rel = root.relativeFilePath(path);
        const QString group = rel.contains(QLatin1Char('/')) ? rel.section(QLatin1Char('/'), 0, 0) : QString();
        if (!groups.contains(group))
            groups.append(group);
        const auto &pal = folderPalette();
        return group.isEmpty() ? pal.at(0) : pal.at(1 + (groups.indexOf(group) % (pal.size() - 1)));
    };

    QStringList paths = index->notes().keys();
    paths.sort(Qt::CaseInsensitive);
    for (const QString &path : std::as_const(paths)) {
        const NoteInfo &info = index->notes()[path];
        addNode(path, info.name, path, Kind::Note, groupColor(path));
    }

    QSet<QPair<int, int>> edgeSet;
    auto addEdge = [&](int a, int b) {
        if (a == b) return;
        const QPair<int, int> key(qMin(a, b), qMax(a, b));
        if (!edgeSet.contains(key)) {
            edgeSet.insert(key);
            edges.append(key);
        }
    };

    for (const QString &path : std::as_const(paths)) {
        const NoteInfo &info = index->notes()[path];
        const int from = ids.value(path);
        for (const NoteLink &link : info.links) {
            const QString resolved = index->resolveLink(link.target);
            if (!resolved.isEmpty()) {
                addEdge(from, ids.value(resolved));
            }
            else if (!link.target.startsWith(QLatin1Char('\x01'))) {
                // Link para uma nota que ainda não existe
                const int ghost = addNode(QStringLiteral("?") + link.target.toLower(), link.target, link.target, Kind::Ghost, QColor(0x6B, 0x6B, 0x8F));
                addEdge(from, ghost);
            }
        }
        if (showTags) {
            for (const QString &tag : info.tags) {
                const int t = addNode(QStringLiteral("#") + tag.toLower(), QStringLiteral("#") + tag, tag, Kind::Tag, QColor::fromRgb(CraftPadHub::TagColor));
                addEdge(from, t);
            }
        }
    }

    // Vizinhos e grau de cada nó
    neighbors = QVector<QVector<int>>(nodes.size());
    for (const auto &e : std::as_const(edges)) {
        neighbors[e.first].append(e.second);
        neighbors[e.second].append(e.first);
    }

    // Remove notas soltas se pedido
    if (!showOrphans) {
        QVector<Node> kept;
        QVector<int> remap(nodes.size(), -1);
        for (int i = 0; i < nodes.size(); ++i) {
            if (!neighbors[i].isEmpty()) {
                remap[i] = kept.size();
                kept.append(nodes[i]);
            }
        }
        QVector<QPair<int, int>> keptEdges;
        for (const auto &e : std::as_const(edges))
            keptEdges.append({remap[e.first], remap[e.second]});
        nodes = kept;
        edges = keptEdges;
        neighbors = QVector<QVector<int>>(nodes.size());
        for (const auto &e : std::as_const(edges)) {
            neighbors[e.first].append(e.second);
            neighbors[e.second].append(e.first);
        }
    }

    // Posição: a antiga, ou perto de um vizinho que já existia, ou numa espiral
    auto *rng = QRandomGenerator::global();
    notes = tags = 0;
    for (int i = 0; i < nodes.size(); ++i) {
        Node &n = nodes[i];
        n.degree = neighbors[i].size();
        const double base = n.kind == Kind::Tag ? 4.5 : (n.kind == Kind::Ghost ? 3.0 : 5.0);
        n.radius = base + 2.4 * qSqrt(n.degree);
        if (n.kind == Kind::Note) ++notes;
        if (n.kind == Kind::Tag) ++tags;

        if (oldState.contains(n.id)) {
            n.pos = oldState.value(n.id).first;
            n.vel = oldState.value(n.id).second;
            continue;
        }
        bool placed = false;
        for (int nb : std::as_const(neighbors[i])) {
            if (oldState.contains(nodes[nb].id)) {
                n.pos = oldState.value(nodes[nb].id).first + QPointF(rng->bounded(40.0) - 20, rng->bounded(40.0) - 20);
                placed = true;
                break;
            }
        }
        if (!placed) {
            // Espiral dourada: começa com cara de galáxia
            const double a = i * 2.39996;
            const double r = 18.0 * qSqrt(i + 1);
            n.pos = QPointF(qCos(a) * r, qSin(a) * r);
        }
    }

    alpha = oldState.isEmpty() ? 1.0 : qMax(alpha, 0.5);
    if (oldState.isEmpty())
        autoFit = true;
    hovered = dragging = -1;
    emit statsChanged();
    update();
}

void GraphView::simulate()
{
    const int count = nodes.size();
    if (count == 0)
        return;

    QVector<QPointF> force(count, QPointF(0, 0));

    // Repulsão entre todos os pares (como cargas elétricas)
    for (int i = 0; i < count; ++i) {
        const QPointF pi = nodes[i].pos;
        for (int j = i + 1; j < count; ++j) {
            QPointF d = pi - nodes[j].pos;
            double dist2 = d.x() * d.x() + d.y() * d.y();
            if (dist2 > 700.0 * 700.0)
                continue;
            if (dist2 < 0.01) {
                d = QPointF(QRandomGenerator::global()->bounded(1.0) - 0.5, QRandomGenerator::global()->bounded(1.0) - 0.5);
                dist2 = 0.25;
            }
            const double dist = qSqrt(dist2);
            const double f = Repulsion / qMax(dist2, 25.0);
            const QPointF push = d / dist * f;
            force[i] += push;
            force[j] -= push;
        }
    }

    // Molas nas ligações
    for (const auto &e : std::as_const(edges)) {
        QPointF d = nodes[e.second].pos - nodes[e.first].pos;
        const double dist = qMax(qSqrt(d.x() * d.x() + d.y() * d.y()), 0.01);
        // Tags com muitas notas ficam com as notas um pouco mais longe
        const double len = SpringLength + 4.0 * qSqrt(qMax(nodes[e.first].degree, nodes[e.second].degree));
        const QPointF pull = d / dist * (dist - len) * SpringStrength;
        force[e.first] += pull;
        force[e.second] -= pull;
    }

    // Gravidade para o centro e integração
    for (int i = 0; i < count; ++i) {
        Node &n = nodes[i];
        if (n.dragged) {
            n.vel = QPointF(0, 0);
            continue;
        }
        force[i] -= n.pos * Gravity * (n.degree == 0 ? 1.8 : 1.0);
        n.vel = (n.vel + force[i] * alpha) * Damping;
        const double speed = qSqrt(n.vel.x() * n.vel.x() + n.vel.y() * n.vel.y());
        if (speed > MaxSpeed)
            n.vel *= MaxSpeed / speed;
        n.pos += n.vel;
    }

    alpha = qMax(MinAlpha, alpha * 0.992);
}

void GraphView::step()
{
    // Vaults muito grandes: calcula a física a cada 2 quadros
    static int frame = 0;
    if (nodes.size() < 1500 || (++frame % 2) == 0)
        simulate();

    const qint64 now = clock.elapsed();

    // Gira devagar quando ninguém está mexendo; para suavemente ao interagir
    const bool idle = idleRotation && dragging < 0 && !panning && (now - lastInteraction) > IdleDelayMs;
    const double targetSpin = idle ? IdleSpin : 0.0;
    spin += (targetSpin - spin) * (idle ? 0.01 : 0.12);
    angle += spin;

    // Brilho: realça o nó sob o mouse e seus vizinhos
    QVector<bool> lit(nodes.size(), hovered < 0);
    if (hovered >= 0) {
        lit[hovered] = true;
        for (int nb : std::as_const(neighbors[hovered]))
            lit[nb] = true;
    }
    for (int i = 0; i < nodes.size(); ++i) {
        const double target = lit[i] ? 1.0 : 0.14;
        nodes[i].glow += (target - nodes[i].glow) * 0.15;
    }

    // Enquadramento automático enquanto a galáxia se forma
    if (autoFit && !nodes.isEmpty()) {
        QRectF bounds;
        for (const Node &n : std::as_const(nodes))
            bounds |= QRectF(n.pos - QPointF(1, 1), QSizeF(2, 2));
        bounds.adjust(-60, -60, 60, 60);
        const double fit = qBound(0.15, qMin(width() / bounds.width(), height() / bounds.height()), 2.0);
        zoom += (fit - zoom) * 0.06;
        camera += (bounds.center() - camera) * 0.06;
        if (alpha < 0.2 && qAbs(fit - zoom) < 0.01)
            autoFit = false;
    }

    update();
}

QPointF GraphView::toScreen(const QPointF &world) const
{
    const QPointF d = world - camera;
    const double c = qCos(angle), s = qSin(angle);
    const QPointF r(d.x() * c - d.y() * s, d.x() * s + d.y() * c);
    return QPointF(width() / 2.0, height() / 2.0) + r * zoom;
}

QPointF GraphView::toWorld(const QPointF &screen) const
{
    const QPointF r = (screen - QPointF(width() / 2.0, height() / 2.0)) / zoom;
    const double c = qCos(-angle), s = qSin(-angle);
    return camera + QPointF(r.x() * c - r.y() * s, r.x() * s + r.y() * c);
}

int GraphView::nodeAt(const QPointF &screen) const
{
    int best = -1;
    double bestDist = 1e9;
    for (int i = 0; i < nodes.size(); ++i) {
        const QPointF d = toScreen(nodes[i].pos) - screen;
        const double dist = qSqrt(d.x() * d.x() + d.y() * d.y());
        const double reach = nodes[i].radius * zoom + 5;
        if (dist < reach && dist < bestDist) {
            best = i;
            bestDist = dist;
        }
    }
    return best;
}

void GraphView::touch()
{
    lastInteraction = clock.elapsed();
}

void GraphView::drawBackground(QPainter &p)
{
    const QPointF center(width() / 2.0, height() / 2.0);
    const double span = qMax(width(), height());

    // Espaço profundo
    QRadialGradient space(center, span * 0.75);
    space.setColorAt(0.0, QColor(0x1B, 0x10, 0x3D));
    space.setColorAt(0.55, QColor(0x0C, 0x07, 0x1F));
    space.setColorAt(1.0, QColor(0x04, 0x02, 0x0B));
    p.fillRect(rect(), space);

    // Nebulosas que giram junto, mais devagar
    p.setCompositionMode(QPainter::CompositionMode_Plus);
    const double na = angle * 0.6;
    const QPointF n1 = center + QPointF(qCos(na), qSin(na)) * span * 0.22;
    const QPointF n2 = center + QPointF(qCos(na + 2.6), qSin(na + 2.6)) * span * 0.26;
    QRadialGradient neb1(n1, span * 0.45);
    neb1.setColorAt(0.0, QColor(155, 107, 224, 46));
    neb1.setColorAt(1.0, QColor(155, 107, 224, 0));
    p.fillRect(rect(), neb1);
    QRadialGradient neb2(n2, span * 0.40);
    neb2.setColorAt(0.0, QColor(255, 46, 151, 30));
    neb2.setColorAt(1.0, QColor(255, 46, 151, 0));
    p.fillRect(rect(), neb2);

    // Estrelas cintilando, com leve paralaxe na rotação
    const double t = clock.elapsed() / 1000.0;
    const double scale = span / 1400.0;
    p.setPen(Qt::NoPen);
    for (const Star &s : std::as_const(stars)) {
        const double a = s.angle + angle * 0.35;
        const QPointF pt = center + QPointF(qCos(a), qSin(a)) * s.dist * scale;
        const int alpha = int(90 + 110 * (0.5 + 0.5 * qSin(t * 1.3 + s.phase)));
        p.setBrush(QColor(220, 215, 255, alpha));
        p.drawEllipse(pt, s.size, s.size);
    }
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
}

void GraphView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    drawBackground(p);

    if (nodes.isEmpty()) {
        p.setPen(QColor(255, 255, 255, 170));
        p.drawText(rect(), Qt::AlignCenter, index->hasRoot()
                   ? tr("Nenhuma nota .md neste vault ainda.")
                   : tr("Escolha a pasta do vault no painel Vault para ver o grafo."));
        return;
    }

    QVector<QPointF> screen(nodes.size());
    for (int i = 0; i < nodes.size(); ++i)
        screen[i] = toScreen(nodes[i].pos);

    const double nodeScale = qBound(0.55, qSqrt(zoom), 2.2);

    // Ligações com brilho aditivo (fica neon sobre o fundo escuro)
    p.setCompositionMode(QPainter::CompositionMode_Plus);
    for (const auto &e : std::as_const(edges)) {
        const Node &a = nodes[e.first];
        const Node &b = nodes[e.second];
        const double g = qMin(a.glow, b.glow);
        const bool hot = hovered >= 0 && (e.first == hovered || e.second == hovered);
        QLinearGradient grad(screen[e.first], screen[e.second]);
        QColor ca = a.color, cb = b.color;
        ca.setAlphaF(qBound(0.0, (hot ? 0.95 : 0.32) * g, 1.0));
        cb.setAlphaF(qBound(0.0, (hot ? 0.95 : 0.32) * g, 1.0));
        grad.setColorAt(0, ca);
        grad.setColorAt(1, cb);
        p.setPen(QPen(QBrush(grad), hot ? 2.2 : 1.1));
        p.drawLine(screen[e.first], screen[e.second]);
    }

    // Halo de cada nó
    p.setPen(Qt::NoPen);
    for (int i = 0; i < nodes.size(); ++i) {
        const Node &n = nodes[i];
        const double r = n.radius * nodeScale;
        const double haloR = r * (n.kind == Kind::Ghost ? 2.0 : 3.6);
        QRadialGradient halo(screen[i], haloR);
        QColor c = n.color;
        c.setAlphaF(0.55 * n.glow);
        halo.setColorAt(0.0, c);
        c.setAlphaF(0.0);
        halo.setColorAt(1.0, c);
        p.setBrush(halo);
        p.drawEllipse(screen[i], haloR, haloR);
    }
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    // Núcleo dos nós
    const double pulse = 0.5 + 0.5 * qSin(clock.elapsed() / 260.0);
    for (int i = 0; i < nodes.size(); ++i) {
        const Node &n = nodes[i];
        const double r = n.radius * nodeScale;
        const double g = qBound(0.0, n.glow, 1.0);

        if (n.kind == Kind::Ghost) {
            QColor c = n.color;
            c.setAlphaF(0.9 * g);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(c, 1.4, Qt::DashLine));
            p.drawEllipse(screen[i], r, r);
            continue;
        }

        QRadialGradient core(screen[i] - QPointF(r * 0.35, r * 0.35), r * 1.3);
        QColor light = n.color.lighter(170);
        QColor base = n.color;
        light.setAlphaF(g);
        base.setAlphaF(g);
        core.setColorAt(0.0, light);
        core.setColorAt(1.0, base);
        p.setBrush(core);

        if (n.kind == Kind::Tag) {
            // Tags: losango, para diferenciar das notas
            QPainterPath diamond;
            const QPointF c = screen[i];
            const double d = r * 1.25;
            diamond.moveTo(c + QPointF(0, -d));
            diamond.lineTo(c + QPointF(d, 0));
            diamond.lineTo(c + QPointF(0, d));
            diamond.lineTo(c + QPointF(-d, 0));
            diamond.closeSubpath();
            p.setPen(QPen(QColor(255, 255, 255, int(90 * g)), 1.0));
            p.drawPath(diamond);
        }
        else {
            p.setPen(Qt::NoPen);
            p.drawEllipse(screen[i], r, r);
        }

        // Anel pulsante na nota aberta no editor
        if (n.kind == Kind::Note && n.payload == currentFile) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(255, 255, 255, int(120 + 120 * pulse)), 2.0));
            p.drawEllipse(screen[i], r + 4 + 3 * pulse, r + 4 + 3 * pulse);
        }
    }

    // Nomes: aparecem ao aproximar, no hover e nos nós mais conectados
    QFont font = this->font();
    font.setPointSizeF(qMax(8.0, font.pointSizeF() * 0.95));
    p.setFont(font);
    const QFontMetrics fm(font);
    const double zoomFade = qBound(0.0, (zoom - 0.55) / 0.5, 1.0);
    for (int i = 0; i < nodes.size(); ++i) {
        const Node &n = nodes[i];
        const bool focus = hovered >= 0 && (i == hovered || neighbors[hovered].contains(i));
        const bool hub = n.degree >= 6;
        double a = focus ? 1.0 : (hovered >= 0 ? 0.0 : qMax(zoomFade, hub ? 0.75 : 0.0));
        if (a <= 0.02)
            continue;
        const QString text = fm.elidedText(n.label, Qt::ElideRight, 220);
        const double r = n.radius * nodeScale;
        const QPointF at = screen[i] + QPointF(-fm.horizontalAdvance(text) / 2.0, r + fm.ascent() + 4);
        p.setPen(QColor(0, 0, 0, int(160 * a)));
        p.drawText(at + QPointF(1, 1), text);
        QColor tc = n.kind == Kind::Tag ? n.color.lighter(140) : QColor(240, 236, 255);
        tc.setAlphaF(a);
        p.setPen(tc);
        p.drawText(at, text);
    }
}

void GraphView::mousePressEvent(QMouseEvent *event)
{
    touch();
    autoFit = false;
    pressPos = lastMouse = event->position();
    moved = false;
    dragging = nodeAt(event->position());
    if (dragging >= 0) {
        nodes[dragging].dragged = true;
        alpha = qMax(alpha, 0.35);
    }
    else {
        panning = true;
        setCursor(Qt::ClosedHandCursor);
    }
}

void GraphView::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    if ((pos - pressPos).manhattanLength() > 4)
        moved = true;

    if (dragging >= 0) {
        touch();
        nodes[dragging].pos = toWorld(pos);
        alpha = qMax(alpha, 0.3);
    }
    else if (panning) {
        touch();
        camera = toWorld(QPointF(width() / 2.0, height() / 2.0) - (pos - lastMouse));
    }
    else {
        const int h = nodeAt(pos);
        if (h != hovered) {
            hovered = h;
            if (h >= 0) touch();
            setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        }
    }
    lastMouse = pos;
}

void GraphView::mouseReleaseEvent(QMouseEvent *event)
{
    touch();
    if (dragging >= 0) {
        nodes[dragging].dragged = false;
        // Clique sem arrastar: abre a nota / mostra a tag
        if (!moved) {
            const Node &n = nodes[dragging];
            CraftPadHub *hub = CraftPadHub::instance();
            if (n.kind == Kind::Note)
                emit hub->openFileRequested(n.payload, -1);
            else if (n.kind == Kind::Tag)
                emit hub->tagRequested(n.payload);
            else
                emit hub->openNoteRequested(n.payload, QString());
        }
    }
    dragging = -1;
    panning = false;
    setCursor(nodeAt(event->position()) >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void GraphView::wheelEvent(QWheelEvent *event)
{
    touch();
    autoFit = false;
    const QPointF anchor = toWorld(event->position());
    const double factor = qPow(1.0015, event->angleDelta().y());
    zoom = qBound(0.1, zoom * factor, 5.0);
    // Mantém o ponto sob o cursor parado
    camera += anchor - toWorld(event->position());
}

void GraphView::leaveEvent(QEvent *)
{
    hovered = -1;
}

void GraphView::showEvent(QShowEvent *)
{
    touch();
    timer->start();
}

void GraphView::hideEvent(QHideEvent *)
{
    timer->stop();
}
