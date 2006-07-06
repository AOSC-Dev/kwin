/*****************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2006 Lubos Lunak <l.lunak@kde.org>

You can Freely distribute this program under the GNU General Public
License. See the file "COPYING" for the exact licensing terms.
******************************************************************/

// TODO MIT or some other licence, perhaps move to some lib

#ifndef KWIN_EFFECTS_H
#define KWIN_EFFECTS_H

#include <qmap.h>
#include <qpoint.h>
#include <qtimer.h>

namespace KWinInternal
{

class Toplevel;
class Workspace;

class Matrix
    {
    public:
        Matrix();
        Matrix& operator*=( const Matrix& m );
        bool isOnlyTranslate() const;
        bool isIdentity() const;
        double xTranslate() const;
        double yTranslate() const;
        double zTranslate() const;
        QPoint transform( const QPoint& p ) const;
        double m[ 4 ][ 4 ];
    };

Matrix operator*( const Matrix& m1, const Matrix& m2 );

inline Matrix& Matrix::operator*=( const Matrix& m )
    {
    return *this = *this * m;
    }

inline double Matrix::xTranslate() const
    {
    return m[ 0 ][ 3 ];
    }

inline double Matrix::yTranslate() const
    {
    return m[ 1 ][ 3 ];
    }

inline double Matrix::zTranslate() const
    {
    return m[ 2 ][ 3 ];
    }

class EffectData
    {
    public:
        double opacity;
    };

class Effect
    {
    public:
        virtual ~Effect();
        // called when moved/resized or once after it's finished
        virtual void windowUserMovedResized( Toplevel* c, bool first, bool last );
        virtual void windowDeleted( Toplevel* c );
        virtual void transformWindow( Toplevel* c, Matrix& m, EffectData& data );
        virtual void transformWorkspace( Matrix& m, EffectData& data );
    };

class EffectsHandler
    {
    public:
        EffectsHandler( Workspace* ws );
        void windowUserMovedResized( Toplevel* c, bool first, bool last );
        void windowDeleted( Toplevel* c );
        void transformWindow( Toplevel* c, Matrix& m, EffectData& data );
        void transformWorkspace( Matrix& m, EffectData& data );
    };

extern EffectsHandler* effects;

class MakeHalfTransparent
    : public Effect
    {
    public:
        virtual void windowUserMovedResized( Toplevel* c, bool first, bool last );
        virtual void transformWindow( Toplevel* c, Matrix& m, EffectData& data );
    };

class ShakyMove
    : public QObject, public Effect
    {
    Q_OBJECT
    public:
        ShakyMove();
        virtual void windowUserMovedResized( Toplevel* c, bool first, bool last );
        virtual void transformWindow( Toplevel* c, Matrix& m, EffectData& data );
        virtual void windowDeleted( Toplevel* c );
    private slots:
        void tick();
    private:
        QMap< Toplevel*, int > windows;
        QTimer timer;
    };

class GrowMove
    : public Effect
    {
    public:
        virtual void windowUserMovedResized( Toplevel* c, bool first, bool last );
        virtual void transformWindow( Toplevel* c, Matrix& m, EffectData& data );
    };

class ShiftWorkspaceUp
    : public QObject, public Effect
    {
    Q_OBJECT
    public:
        ShiftWorkspaceUp( Workspace* ws );
        virtual void transformWorkspace( Matrix& m, EffectData& data );
    private slots:
        void tick();
    private:
        QTimer timer;
        bool up;
        Workspace* wspace;
    };

} // namespace

#endif
