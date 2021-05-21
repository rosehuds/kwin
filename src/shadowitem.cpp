/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "shadowitem.h"
#include "shadow.h"

namespace KWin
{

ShadowItem::ShadowItem(Shadow *shadow, Scene::Window *window, Item *parent)
    : Item(window, parent)
    , m_shadow(shadow)
{
    connect(shadow, &Shadow::regionChanged, this, &ShadowItem::handleRegionChanged);
    connect(shadow, &Shadow::textureChanged, this, &ShadowItem::handleTextureChanged);

    handleRegionChanged();
    handleTextureChanged();
}

ShadowItem::~ShadowItem()
{
}

Shadow *ShadowItem::shadow() const
{
    return m_shadow.data();
}

void ShadowItem::handleRegionChanged()
{
    const QRect rect = shadow()->shadowRegion().boundingRect();

    setPosition(rect.topLeft());
    setSize(rect.size());
    discardQuads();
}

void ShadowItem::handleTextureChanged()
{
    scheduleRepaint(rect());
    discardQuads();
}

static inline void distributeHorizontally(QRectF &leftRect, QRectF &rightRect)
{
    if (leftRect.right() > rightRect.left()) {
        const qreal boundedRight = std::min(leftRect.right(), rightRect.right());
        const qreal boundedLeft = std::max(leftRect.left(), rightRect.left());
        const qreal halfOverlap = (boundedRight - boundedLeft) / 2.0;
        leftRect.setRight(boundedRight - halfOverlap);
        rightRect.setLeft(boundedLeft + halfOverlap);
    }
}

static inline void distributeVertically(QRectF &topRect, QRectF &bottomRect)
{
    if (topRect.bottom() > bottomRect.top()) {
        const qreal boundedBottom = std::min(topRect.bottom(), bottomRect.bottom());
        const qreal boundedTop = std::max(topRect.top(), bottomRect.top());
        const qreal halfOverlap = (boundedBottom - boundedTop) / 2.0;
        topRect.setBottom(boundedBottom - halfOverlap);
        bottomRect.setTop(boundedTop + halfOverlap);
    }
}

WindowQuadList ShadowItem::buildQuads() const
{
    const Toplevel *toplevel = window()->window();

    // Do not draw shadows if window width or window height is less than 5 px. 5 is an arbitrary choice.
    if (!toplevel->wantsShadowToBeRendered() || toplevel->width() < 5 || toplevel->height() < 5) {
        return WindowQuadList();
    }

    const QSizeF top(m_shadow->elementSize(Shadow::ShadowElementTop));
    const QSizeF topRight(m_shadow->elementSize(Shadow::ShadowElementTopRight));
    const QSizeF right(m_shadow->elementSize(Shadow::ShadowElementRight));
    const QSizeF bottomRight(m_shadow->elementSize(Shadow::ShadowElementBottomRight));
    const QSizeF bottom(m_shadow->elementSize(Shadow::ShadowElementBottom));
    const QSizeF bottomLeft(m_shadow->elementSize(Shadow::ShadowElementBottomLeft));
    const QSizeF left(m_shadow->elementSize(Shadow::ShadowElementLeft));
    const QSizeF topLeft(m_shadow->elementSize(Shadow::ShadowElementTopLeft));

    const QMarginsF shadowMargins(
            std::max({topLeft.width(), left.width(), bottomLeft.width()}),
            std::max({topLeft.height(), top.height(), topRight.height()}),
            std::max({topRight.width(), right.width(), bottomRight.width()}),
            std::max({bottomRight.height(), bottom.height(), bottomLeft.height()}));

    const QRectF outerRect(QPointF(-m_shadow->leftOffset(), -m_shadow->topOffset()),
                           QPointF(toplevel->width() + m_shadow->rightOffset(),
                                   toplevel->height() + m_shadow->bottomOffset()));

    const int width = shadowMargins.left() + std::max(top.width(), bottom.width()) + shadowMargins.right();
    const int height = shadowMargins.top() + std::max(left.height(), right.height()) + shadowMargins.bottom();

    QRectF topLeftRect;
    if (!topLeft.isEmpty()) {
        topLeftRect = QRectF(outerRect.topLeft(), topLeft);
    } else {
        topLeftRect = QRectF(outerRect.left() + shadowMargins.left(),
                             outerRect.top() + shadowMargins.top(), 0, 0);
    }

    QRectF topRightRect;
    if (!topRight.isEmpty()) {
        topRightRect = QRectF(outerRect.right() - topRight.width(), outerRect.top(),
                              topRight.width(), topRight.height());
    } else {
        topRightRect = QRectF(outerRect.right() - shadowMargins.right(),
                              outerRect.top() + shadowMargins.top(), 0, 0);
    }

    QRectF bottomRightRect;
    if (!bottomRight.isEmpty()) {
        bottomRightRect = QRectF(outerRect.right() - bottomRight.width(),
                                 outerRect.bottom() - bottomRight.height(),
                                 bottomRight.width(), bottomRight.height());
    } else {
        bottomRightRect = QRectF(outerRect.right() - shadowMargins.right(),
                                 outerRect.bottom() - shadowMargins.bottom(), 0, 0);
    }

    QRectF bottomLeftRect;
    if (!bottomLeft.isEmpty()) {
        bottomLeftRect = QRectF(outerRect.left(), outerRect.bottom() - bottomLeft.height(),
                                bottomLeft.width(), bottomLeft.height());
    } else {
        bottomLeftRect = QRectF(outerRect.left() + shadowMargins.left(),
                                outerRect.bottom() - shadowMargins.bottom(), 0, 0);
    }

    // Re-distribute the corner tiles so no one of them is overlapping with others.
    // By doing this, we assume that shadow's corner tiles are symmetric
    // and it is OK to not draw top/right/bottom/left tile between corners.
    // For example, let's say top-left and top-right tiles are overlapping.
    // In that case, the right side of the top-left tile will be shifted to left,
    // the left side of the top-right tile will shifted to right, and the top
    // tile won't be rendered.
    distributeHorizontally(topLeftRect, topRightRect);
    distributeHorizontally(bottomLeftRect, bottomRightRect);
    distributeVertically(topLeftRect, bottomLeftRect);
    distributeVertically(topRightRect, bottomRightRect);

    qreal tx1 = 0.0,
          tx2 = 0.0,
          ty1 = 0.0,
          ty2 = 0.0;

    WindowQuadList quads;
    quads.reserve(8);
    void *tag = const_cast<ShadowItem *>(this);

    if (topLeftRect.isValid()) {
        tx1 = 0.0;
        ty1 = 0.0;
        tx2 = topLeftRect.width() / width;
        ty2 = topLeftRect.height() / height;
        WindowQuad topLeftQuad(WindowQuadShadow, tag);
        topLeftQuad[0] = WindowVertex(topLeftRect.left(),  topLeftRect.top(),    tx1, ty1);
        topLeftQuad[1] = WindowVertex(topLeftRect.right(), topLeftRect.top(),    tx2, ty1);
        topLeftQuad[2] = WindowVertex(topLeftRect.right(), topLeftRect.bottom(), tx2, ty2);
        topLeftQuad[3] = WindowVertex(topLeftRect.left(),  topLeftRect.bottom(), tx1, ty2);
        quads.append(topLeftQuad);
    }

    if (topRightRect.isValid()) {
        tx1 = 1.0 - topRightRect.width() / width;
        ty1 = 0.0;
        tx2 = 1.0;
        ty2 = topRightRect.height() / height;
        WindowQuad topRightQuad(WindowQuadShadow, tag);
        topRightQuad[0] = WindowVertex(topRightRect.left(),  topRightRect.top(),    tx1, ty1);
        topRightQuad[1] = WindowVertex(topRightRect.right(), topRightRect.top(),    tx2, ty1);
        topRightQuad[2] = WindowVertex(topRightRect.right(), topRightRect.bottom(), tx2, ty2);
        topRightQuad[3] = WindowVertex(topRightRect.left(),  topRightRect.bottom(), tx1, ty2);
        quads.append(topRightQuad);
    }

    if (bottomRightRect.isValid()) {
        tx1 = 1.0 - bottomRightRect.width() / width;
        tx2 = 1.0;
        ty1 = 1.0 - bottomRightRect.height() / height;
        ty2 = 1.0;
        WindowQuad bottomRightQuad(WindowQuadShadow, tag);
        bottomRightQuad[0] = WindowVertex(bottomRightRect.left(),  bottomRightRect.top(),    tx1, ty1);
        bottomRightQuad[1] = WindowVertex(bottomRightRect.right(), bottomRightRect.top(),    tx2, ty1);
        bottomRightQuad[2] = WindowVertex(bottomRightRect.right(), bottomRightRect.bottom(), tx2, ty2);
        bottomRightQuad[3] = WindowVertex(bottomRightRect.left(),  bottomRightRect.bottom(), tx1, ty2);
        quads.append(bottomRightQuad);
    }

    if (bottomLeftRect.isValid()) {
        tx1 = 0.0;
        tx2 = bottomLeftRect.width() / width;
        ty1 = 1.0 - bottomLeftRect.height() / height;
        ty2 = 1.0;
        WindowQuad bottomLeftQuad(WindowQuadShadow, tag);
        bottomLeftQuad[0] = WindowVertex(bottomLeftRect.left(),  bottomLeftRect.top(),    tx1, ty1);
        bottomLeftQuad[1] = WindowVertex(bottomLeftRect.right(), bottomLeftRect.top(),    tx2, ty1);
        bottomLeftQuad[2] = WindowVertex(bottomLeftRect.right(), bottomLeftRect.bottom(), tx2, ty2);
        bottomLeftQuad[3] = WindowVertex(bottomLeftRect.left(),  bottomLeftRect.bottom(), tx1, ty2);
        quads.append(bottomLeftQuad);
    }

    QRectF topRect(QPointF(topLeftRect.right(), outerRect.top()),
                   QPointF(topRightRect.left(), outerRect.top() + top.height()));

    QRectF rightRect(QPointF(outerRect.right() - right.width(), topRightRect.bottom()),
                     QPointF(outerRect.right(), bottomRightRect.top()));

    QRectF bottomRect(QPointF(bottomLeftRect.right(), outerRect.bottom() - bottom.height()),
                      QPointF(bottomRightRect.left(), outerRect.bottom()));

    QRectF leftRect(QPointF(outerRect.left(), topLeftRect.bottom()),
                    QPointF(outerRect.left() + left.width(), bottomLeftRect.top()));

    // Re-distribute left/right and top/bottom shadow tiles so they don't
    // overlap when the window is too small. Please notice that we don't
    // fix overlaps between left/top(left/bottom, right/top, and so on)
    // corner tiles because corresponding counter parts won't be valid when
    // the window is too small, which means they won't be rendered.
    distributeHorizontally(leftRect, rightRect);
    distributeVertically(topRect, bottomRect);

    if (topRect.isValid()) {
        tx1 = shadowMargins.left() / width;
        ty1 = 0.0;
        tx2 = tx1 + top.width() / width;
        ty2 = topRect.height() / height;
        WindowQuad topQuad(WindowQuadShadow, tag);
        topQuad[0] = WindowVertex(topRect.left(),  topRect.top(),    tx1, ty1);
        topQuad[1] = WindowVertex(topRect.right(), topRect.top(),    tx2, ty1);
        topQuad[2] = WindowVertex(topRect.right(), topRect.bottom(), tx2, ty2);
        topQuad[3] = WindowVertex(topRect.left(),  topRect.bottom(), tx1, ty2);
        quads.append(topQuad);
    }

    if (rightRect.isValid()) {
        tx1 = 1.0 - rightRect.width() / width;
        ty1 = shadowMargins.top() / height;
        tx2 = 1.0;
        ty2 = ty1 + right.height() / height;
        WindowQuad rightQuad(WindowQuadShadow, tag);
        rightQuad[0] = WindowVertex(rightRect.left(),  rightRect.top(),    tx1, ty1);
        rightQuad[1] = WindowVertex(rightRect.right(), rightRect.top(),    tx2, ty1);
        rightQuad[2] = WindowVertex(rightRect.right(), rightRect.bottom(), tx2, ty2);
        rightQuad[3] = WindowVertex(rightRect.left(),  rightRect.bottom(), tx1, ty2);
        quads.append(rightQuad);
    }

    if (bottomRect.isValid()) {
        tx1 = shadowMargins.left() / width;
        ty1 = 1.0 - bottomRect.height() / height;
        tx2 = tx1 + bottom.width() / width;
        ty2 = 1.0;
        WindowQuad bottomQuad(WindowQuadShadow, tag);
        bottomQuad[0] = WindowVertex(bottomRect.left(),  bottomRect.top(),    tx1, ty1);
        bottomQuad[1] = WindowVertex(bottomRect.right(), bottomRect.top(),    tx2, ty1);
        bottomQuad[2] = WindowVertex(bottomRect.right(), bottomRect.bottom(), tx2, ty2);
        bottomQuad[3] = WindowVertex(bottomRect.left(),  bottomRect.bottom(), tx1, ty2);
        quads.append(bottomQuad);
    }

    if (leftRect.isValid()) {
        tx1 = 0.0;
        ty1 = shadowMargins.top() / height;
        tx2 = leftRect.width() / width;
        ty2 = ty1 + left.height() / height;
        WindowQuad leftQuad(WindowQuadShadow, tag);
        leftQuad[0] = WindowVertex(leftRect.left(),  leftRect.top(),    tx1, ty1);
        leftQuad[1] = WindowVertex(leftRect.right(), leftRect.top(),    tx2, ty1);
        leftQuad[2] = WindowVertex(leftRect.right(), leftRect.bottom(), tx2, ty2);
        leftQuad[3] = WindowVertex(leftRect.left(),  leftRect.bottom(), tx1, ty2);
        quads.append(leftQuad);
    }

    return quads;
}

} // namespace KWin
