/*
 * NotepadNext - recursos de notas no estilo Obsidian (vault, links, tags, grafo).
 * Licença GPL-3.0 (a mesma do projeto original, dail8859/NotepadNext).
 */

#ifndef GRAPHVIEW_H
#define GRAPHVIEW_H

#include <QColor>
#include <QElapsedTimer>
#include <QPointF>
#include <QVector>
#include <QWidget>

class QTimer;
class VaultIndex;

// "Galáxia" de notas e tags: simulação de forças (repulsão entre todos,
// molas nas ligações, gravidade para o centro) desenhada com brilho neon.
// Quando ninguém mexe, o conjunto gira devagar.
class GraphView : public QWidget
{
    Q_OBJECT

public:
    explicit GraphView(VaultIndex *index, QWidget *parent = nullptr);

    void setShowTags(bool show);
    void setShowOrphans(bool show);
    void setIdleRotation(bool on);
    void setCurrentFile(const QString &path);
    void fitToView();

    int noteCount() const { return notes; }
    int tagCount() const { return tags; }

signals:
    void statsChanged();

public slots:
    void rebuild();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    enum class Kind { Note, Tag, Ghost };

    struct Node {
        QString id;
        QString label;
        QString payload; // caminho da nota, nome da tag ou alvo do link
        Kind kind = Kind::Note;
        QPointF pos;
        QPointF vel;
        QColor color;
        double radius = 5;
        double glow = 1.0;   // brilho atual (anima ao passar o mouse)
        int degree = 0;
        bool dragged = false;
    };

    void step();
    void simulate();
    QPointF toScreen(const QPointF &world) const;
    QPointF toWorld(const QPointF &screen) const;
    int nodeAt(const QPointF &screen) const;
    void touch(); // registra interação (para de girar)
    void drawBackground(QPainter &p);

    VaultIndex *index;
    QTimer *timer;
    QElapsedTimer clock;
    qint64 lastInteraction = 0;

    QVector<Node> nodes;
    QVector<QPair<int, int>> edges;
    QVector<QVector<int>> neighbors;

    // Estrelas de fundo (posição polar, brilho, fase)
    struct Star { double angle, dist, size, phase; };
    QVector<Star> stars;

    // Câmera
    QPointF camera;      // ponto do mundo no centro da tela
    double zoom = 1.0;
    double angle = 0.0;  // rotação da galáxia
    double spin = 0.0;   // velocidade atual de rotação
    bool autoFit = true;

    // Física
    double alpha = 1.0;  // "temperatura": alta logo após mudanças

    // Interação
    int hovered = -1;
    int dragging = -1;
    bool panning = false;
    bool moved = false;
    QPointF pressPos;
    QPointF lastMouse;

    // Opções
    bool showTags = true;
    bool showOrphans = true;
    bool idleRotation = true;
    QString currentFile;

    int notes = 0;
    int tags = 0;
};

#endif // GRAPHVIEW_H
