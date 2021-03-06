/************************************************************************
*
* This file is part of SuperCollider Qt GUI.
*
* Copyright 2013 Jakob Leben (jakob.leben@gmail.com)
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
************************************************************************/

#include "primitives.h"
#include "../image.h"
#include "../QcApplication.h"
#include "../Common.h"
#include "../type_codec.hpp"
#include "../painting.h"
#include "../hacks/hacks_qt.hpp"

#include <PyrKernel.h>
#include <GC.h>

#include <QImage>
#include <QUrl>
#include <QPainter>
#include <QImageReader>
#include <QImageWriter>
#include <QEventLoop>
#include <QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>

#include <cassert>

namespace QC = QtCollider;

namespace QtCollider {

QPainter *imgPainter = 0;

inline QC::Image * to_image( struct PyrObject * obj )
{
    SharedImage *shared_image_ptr =
            reinterpret_cast<SharedImage*>( slotRawPtr(obj->slots) );
    return shared_image_ptr->data();
}

inline QC::Image * to_image( PyrSlot * slot )
{
    SharedImage *shared_image_ptr =
            reinterpret_cast<SharedImage*>( slotRawPtr( slotRawObject(slot)->slots ) );
    return shared_image_ptr->data();
}

inline QRgb color_to_pixel( const QColor & color )
{
    int r, g, b, a;
    color.getRgb( &r, &g, &b, &a );
    qreal k = a / 255.f;
    r *= k;
    g *= k;
    b *= k;
    QRgb pixel = (a << 24) | (r << 16) | (g << 8) | b;
    return pixel;
}

inline QColor pixel_to_color( QRgb pixel )
{
    int r, g, b, a;
    int mask = 0xFF;
    a = pixel >> 24 & mask;
    r = pixel >> 16 & mask;
    g = pixel << 8 & mask;
    b = pixel & mask;
    if ( a > 0 ) {
        qreal k = a > 0 ? 255.f / a : 0.f;
        r *= k;
        g *= k;
        b *= k;
        return QColor(r,g,b,a);
    }
    else
        return QColor(0,0,0,0);
}

int finalize_image_object( struct VMGlobals *g, struct PyrObject *obj )
{
    SharedImage *shared_image_ptr =
            reinterpret_cast<SharedImage*>( slotRawPtr(obj->slots) );
    delete shared_image_ptr;
    SetNil( obj->slots+0 );
    return errNone;
}

void initialize_image_object( struct VMGlobals *g, struct PyrObject *obj, Image *image )
{
    assert( IsNil( obj->slots ) && IsNil( obj->slots+1 ) );
    SharedImage *shared_image_ptr = new SharedImage(image);
    SetPtr( obj->slots, shared_image_ptr ); // dataptr
    InstallFinalizer( g, obj, 1, finalize_image_object ); // finalizer
}

QC_LANG_PRIMITIVE( QImage_NewPath, 1, PyrSlot *r, PyrSlot *a, VMGlobals *g )
{
    if( !QcApplication::compareThread() ) return QtCollider::wrongThreadError();

  QString path( QtCollider::get<QString>(a) );
  QPixmap pixmap(path);
  if (pixmap.isNull())
      return errFailed;

  Image *image = new Image();
  image->setPixmap(pixmap);
  initialize_image_object(g, slotRawObject(r), image);
  return errNone;
}

QC_LANG_PRIMITIVE( QImage_NewURL, 2, PyrSlot *r, PyrSlot *a, VMGlobals *g )
{
    // FIXME:
    // We can not run an event loop while waiting to receive data, because that
    // would allow GUI to try and call into the language, resulting in a deadlock.
    qcErrorMsg("QImage: loading from URL not yet implemented.");
    return errFailed;
#if 0
  QString url_str = QtCollider::get(a);
  QUrl url(url_str);
  if( !url.isValid() || url.isEmpty() ) {
    qcErrorMsg( QString("QImage: invalid or empty URL: ") + url_str );
    return errFailed;
  }

  if( QURL_IS_LOCAL_FILE(url) ) {
    if( QImage_InitPath( g, slotRawObject(r), url.toLocalFile() ) ) {
      return errNone;
    } else {
      qcErrorMsg( QString("QImage: file doesn't exist or can't be opened: ") + url_str );
      return errFailed;
    }
  }

  if( !IsFloat(a+1) && !IsInt(a+1) ) return errWrongType;
  // Take a safe read to allow Integers:
  float timeout = QtCollider::get(a+1);

  QNetworkAccessManager manager;
  QScopedPointer<QNetworkReply> reply( manager.get( QNetworkRequest(url) ) );

  QEventLoop loop;
  QcApplication::connect( &manager, SIGNAL(finished(QNetworkReply*)), &loop, SLOT(quit()) );
  QcApplication::connect( reply.data(), SIGNAL(error(QNetworkReply::NetworkError)), &loop, SLOT(quit()) );
  QTimer::singleShot( 100 * timeout, &loop, SLOT(quit()) );
  loop.exec(); // blocks

  if( reply->error() != QNetworkReply::NoError ) {
    qcErrorMsg( QString("QImage: error trying to download: ") + url_str );
    return errFailed;
  }
  else if( !reply->isFinished() ) {
    qcErrorMsg( QString("QImage: timeout while trying to download: ") + url_str );
    reply->abort();
    return errFailed;
  }

  QByteArray byteArray = reply->readAll();
  if( byteArray.isEmpty() ) {
    qcErrorMsg( QString("QImage: no data received: ") + url_str );
    return errFailed;
  }

  if( QImage_InitFromData( g, slotRawObject(r), byteArray ) ) {
    return errNone;
  }
  else {
    qcErrorMsg( QString("QImage: failed trying to open downloaded data: ") + url_str );
    return errFailed;
  }
#endif
}

QC_LANG_PRIMITIVE( QImage_NewEmpty, 2, PyrSlot *r, PyrSlot *a, VMGlobals *g )
{
    if( !QcApplication::compareThread() ) return QtCollider::wrongThreadError();

  if( NotInt(a) || NotInt(a+1) ) return errWrongType;
  int width = QtCollider::read<int>(a);
  int height = QtCollider::read<int>(a+1);

  QImage qimage(width, height, QImage::Format_ARGB32_Premultiplied);
  qimage.fill( QColor(Qt::transparent).rgba() );

  Image *image = new Image;
  image->setImage(qimage);
  initialize_image_object(g, slotRawObject(r), image);

  return errNone;
}

QC_LANG_PRIMITIVE( QImage_NewFromWindow, 2, PyrSlot *r, PyrSlot *a, VMGlobals *g )
{
    if( !QcApplication::compareThread() ) return QtCollider::wrongThreadError();

  QObjectProxy *proxy = QtCollider::get(a);
  if( !proxy ) return errWrongType;
  QWidget *widget = qobject_cast<QWidget *>( proxy->object() );
  if( !widget ) return errWrongType;

  QRect rect;
  if( IsObj(a+1) && slotRawObject(a+1)->classptr == SC_CLASS(Rect) )
      rect = QtCollider::read<QRect>(a+1);
  else if (NotNil(a+1))
      return errWrongType;

  QPixmap pixmap = QPixmap::grabWidget( widget, rect );
  if (pixmap.isNull())
      return errFailed;

  Image *image = new Image;
  image->setPixmap(pixmap);
  initialize_image_object(g, slotRawObject(r), image);

  return errNone;
}

QC_LANG_PRIMITIVE( QImage_Free, 0, PyrSlot *r, PyrSlot *a, VMGlobals *g )
{
    if( !QcApplication::compareThread() ) return QtCollider::wrongThreadError();

  Image *image = to_image(r);
  if (image->isPainting()) {
      qcErrorMsg("QImage: can not free while being painted.");
      return errFailed;
  }

  image->clear();
  return errNone;
}

QC_LANG_PRIMITIVE( QImage_HasSmoothTransformation, 0, PyrSlot *r, PyrSlot *a, VMGlobals *g )
{
    if( !QcApplication::compareThread() ) return QtCollider::wrongThreadError();
    Image *image = to_image(r);
    QC::write<bool>( r, image->transformationMode == Qt::SmoothTransformation );
    return errNone;
}

QC_LANG_PRIMITIVE( QImage_SetSmoothTransformation, 1, PyrSlot *r, PyrSlot *a, VMGlobals *g )
{
    if( !QcApplication::compareThread() ) return QtCollider::wrongThreadError();
    bool smooth = QC::get(a);
    Image *image = to_image(r);
    image->transformationMode = smooth ? Qt::SmoothTransformation : Qt::FastTransformation;
    return errNone;
}

QC_LANG_PRIMITIVE( QImage_Width, 0, PyrSlot *r, PyrSlot *a, VMGlobals *g )
{
    if( !QcApplication::compareThread() ) return QtCollider::wrongThreadError();

  Image *image = to_image(r);
  SetInt( r, image->width() );
  return errNone;
}

QC_LANG_PRIMITIVE( QImage_Height, 0, PyrSlot *r, PyrSlot *a, VMGlobals *g )
{
    if( !QcApplication::compareThread() ) return QtCollider::wrongThreadError();

  Image *image = to_image(r);
  SetInt( r, image->height() );
  return errNone;
}

QC_LANG_PRIMITIVE( QImage_SetSize, 3, PyrSlot *r, PyrSlot *a, VMGlobals *g )
{
    if( !QcApplication::compareThread() ) return QtCollider::wrongThreadError();

    if( NotInt(a) || NotInt(a+1) || NotInt(a+2) ) return errWrongType;
    QSize new_size( QtCollider::read<int>(a), QtCollider::read<int>(a+1) );
    int resize_mode = QtCollider::read<int>(a+2);

    Image *image = to_image(r);
    if (image->isPainting()) {
        qcErrorMsg("QImage: can not resize while being painted.");
        return errFailed;
    }

    image->resize( new_size, resize_mode );

    return errNone;
}

QC_LANG_PRIMITIVE( QImage_Write, 3, PyrSlot *r, PyrSlot *a, VMGlobals *g )
{
    if( !QcApplication::compareThread() ) return QtCollider::wrongThreadError();

  QString path = QC::get(a);
  QString format = QC::get(a+1);

  if(NotInt(a+2)) return errWrongType;
  int quality = QC::read<int>(a+2);

  QImage & image = to_image(r)->image();
  if( image.save(path, format.toUpper().toStdString().c_str(), quality) )
    return errNone;

  qcErrorMsg( QString("QImage: Failed to write to file: ") + path );
  return errFailed;
}

QC_LANG_PRIMITIVE( QImage_SetPainter, 0, PyrSlot *r, PyrSlot *a, VMGlobals *g )
{
  if( !QcApplication::compareThread() )
    return QtCollider::wrongThreadError();

  if( QtCollider::paintingAnnounced() ) {
    qcDebugMsg(1, "WARNING: Custom painting already in progress. Will not paint." );
    return errFailed;
  }

  Image *image = to_image(r);
  assert( !image->isPainting() );

  assert( imgPainter == 0 );
  imgPainter = new QPainter( &image->image() );
  QtCollider::announcePainting();
  QtCollider::beginPainting( imgPainter );

  image->setPainting(true);

  return errNone;
}

QC_LANG_PRIMITIVE( QImage_UnsetPainter, 0, PyrSlot *r, PyrSlot *a, VMGlobals *g )
{
  if( !QcApplication::compareThread() )
    return QtCollider::wrongThreadError();

  Image *image = to_image(r);

  assert( image->isPainting() );
  image->setPainting(false);

  assert( imgPainter != 0 );
  QtCollider::endPainting();
  delete imgPainter;
  imgPainter = 0;

  return errNone;
}

QC_LANG_PRIMITIVE( QImage_GetPixel, 2, PyrSlot *r, PyrSlot *a, VMGlobals *g )
{
    if( !QcApplication::compareThread() ) return QtCollider::wrongThreadError();

  if( NotInt(a) || NotInt(a+1)) return errWrongType;
  int x = QC::read<int>(a);
  int y = QC::read<int>(a+1);

  QImage & image = to_image(r)->image();

  if( x >= image.width() || y >= image.height() )
    return errIndexOutOfRange;

  int *line = reinterpret_cast<int*>( image.scanLine(y) );
  SetInt( r, line[x] );

  return errNone;
}

QC_LANG_PRIMITIVE( QImage_GetColor, 2, PyrSlot *r, PyrSlot *a, VMGlobals *g )
{
    if( !QcApplication::compareThread() ) return QtCollider::wrongThreadError();

  if( NotInt(a) || NotInt(a+1)) return errWrongType;
  int x = QC::read<int>(a);
  int y = QC::read<int>(a+1);

  QImage & image = to_image(r)->image();

  if( x >= image.width() || y >= image.height() )
    return errIndexOutOfRange;

  QRgb *line = reinterpret_cast<QRgb*>( image.scanLine(y) );
  QC::set(r, pixel_to_color(line[x]) );

  return errNone;
}

QC_LANG_PRIMITIVE( QImage_SetPixel, 3, PyrSlot *r, PyrSlot *a, VMGlobals *g )
{
    if( !QcApplication::compareThread() ) return QtCollider::wrongThreadError();

  if( NotInt(a) || NotInt(a+1) || NotInt(a+2) ) return errWrongType;
  int pixel = QC::read<int>(a);
  int x = QC::read<int>(a+1);
  int y = QC::read<int>(a+2);

  QImage & image = to_image(r)->image();

  if( x >= image.width() || y >= image.height() )
    return errIndexOutOfRange;

  int *line = reinterpret_cast<int*>( image.scanLine(y) );
  line[x] = pixel;

  return errNone;
}

QC_LANG_PRIMITIVE( QImage_SetColor, 3, PyrSlot *r, PyrSlot *a, VMGlobals *g )
{
    if( !QcApplication::compareThread() ) return QtCollider::wrongThreadError();

  if ( NotObj(a) || slotRawObject(a)->classptr != SC_CLASS(Color)
       || NotInt(a+1) || NotInt(a+2) )
    return errWrongType;
  QColor color( QC::read<QColor>(a) );
  int x = QC::read<int>(a+1);
  int y = QC::read<int>(a+2);

  QImage & image = to_image(r)->image();

  if( x >= image.width() || y >= image.height() )
    return errIndexOutOfRange;

  QRgb *line = reinterpret_cast<QRgb*>( image.scanLine(y) );
  line[x] = color_to_pixel(color);

  return errNone;
}

QC_LANG_PRIMITIVE( QImage_TransferPixels, 4, PyrSlot *r, PyrSlot *a, VMGlobals *g )
{
    if( !QcApplication::compareThread() ) return QtCollider::wrongThreadError();

    if (!isKindOfSlot(a, class_int32array)) {
        qcErrorMsg("QImage: array argument is not a Int32Array");
        return errWrongType;
    }

    if( NotInt(a+2) ) return errWrongType;
    int start = QC::read<int>(a+2);

    if( !(IsTrue(a+3) || IsFalse(a+3)) ) return errWrongType;
    bool store = IsTrue(a+3); // t/f g/s

    QImage &image = to_image(r)->image();
    QRect rect;

    if( IsNil(a+1) ) {
        rect = image.rect();
    }
    else {
        if (!isKindOfSlot(a+1, SC_CLASS(Rect)))
            return errWrongType;
        rect = QC::read<QRect>(a+1);
        if (rect.isEmpty())
            return errNone;
        if( !image.rect().contains(rect) ) {
            qcErrorMsg("QImage: source rectangle out of image bounds");
            return errFailed;
        }
    }

    PyrInt32Array* array = reinterpret_cast<PyrInt32Array*>( slotRawObject(a) );
    QRgb *pixelData = reinterpret_cast<QRgb*>( array->i ) + start;

    int width = rect.width();
    int height = rect.height();
    int size = width * height;
    int x = rect.x();
    int y = rect.y();
    int max_x = width + x;
    int max_y = height + y;

    if( array->size - start < size )
        return errIndexOutOfRange;

    if(store) {
        for( int iy = y; iy < max_y; ++iy ) {
            QRgb *line = reinterpret_cast<QRgb*>( image.scanLine(iy) );
            for( int ix = x; ix < max_x; ++ix ) {
                line[ix] = *pixelData;
                ++pixelData;
            }
        }
    } else {
        for( int iy = y; iy < max_y; ++iy ) {
            QRgb *line = reinterpret_cast<QRgb*>( image.scanLine(iy) );
            for( int ix = x; ix < max_x; ++ix ) {
                *pixelData = line[ix];
                ++pixelData;
            }
        }
    }

    return errNone;
}

QC_LANG_PRIMITIVE( QImage_Fill, 1, PyrSlot *r, PyrSlot *a, VMGlobals *g )
{
    if( !QcApplication::compareThread() ) return QtCollider::wrongThreadError();

  if( NotObj(a) || slotRawObject(a)->classptr != SC_CLASS(Color) )
      return errWrongType;

  QColor color = QC::read<QColor>(a);
  QImage &image = to_image(r)->image();
  image.fill( color_to_pixel(color) );

  return errNone;
}

QC_LANG_PRIMITIVE( QImage_Formats, 1, PyrSlot *r, PyrSlot *a, VMGlobals *g )
{
    if( !QcApplication::compareThread() ) return QtCollider::wrongThreadError();

    if( NotInt(a) ) return errWrongType;
    int rw = QC::read<int>(a);

    QList<QByteArray> formats =
            rw ? QImageWriter::supportedImageFormats() : QImageReader::supportedImageFormats();

    PyrObject *array = newPyrArray( g->gc, formats.size(), 0, true );
    SetObject(r, array);
    for( int i = 0; i < formats.size(); ++i ) {
        PyrString *str = newPyrString( g->gc, formats[i].constData(), obj_immutable, false );
        SetObject(array->slots+i, str);
        ++array->size;
        g->gc->GCWrite( array, array->slots+i );
    }

    return errNone;
}

QC_LANG_PRIMITIVE( QImage_PixelToColor, 1, PyrSlot *r, PyrSlot *a, VMGlobals *g )
{
    if( NotInt(a) ) return errWrongType;
    QRgb pixel = static_cast<QRgb>( QC::read<int>(a) );
    QC::set( r, pixel_to_color(pixel) );
    return errNone;
}

QC_LANG_PRIMITIVE( QImage_ColorToPixel, 1, PyrSlot *r, PyrSlot *a, VMGlobals *g )
{
   if ( NotObj(a) || slotRawObject(a)->classptr != SC_CLASS(Color) )
       return errWrongType;
   QColor color = QC::read<QColor>(a);
   int pixel = static_cast<int>( color_to_pixel(color) );
   QC::set( r, pixel );
   return errNone;
}

void defineQImagePrimitives()
{
  LangPrimitiveDefiner definer;
  definer.define<QImage_NewPath>();
  definer.define<QImage_NewURL>();
  definer.define<QImage_NewEmpty>();
  definer.define<QImage_NewFromWindow>();
  definer.define<QImage_Free>();
  definer.define<QImage_HasSmoothTransformation>();
  definer.define<QImage_SetSmoothTransformation>();
  definer.define<QImage_Width>();
  definer.define<QImage_Height>();
  definer.define<QImage_SetSize>();
  definer.define<QImage_Write>();
  definer.define<QImage_SetPainter>();
  definer.define<QImage_UnsetPainter>();
  definer.define<QImage_GetPixel>();
  definer.define<QImage_GetColor>();
  definer.define<QImage_SetPixel>();
  definer.define<QImage_SetColor>();
  definer.define<QImage_TransferPixels>();
  definer.define<QImage_Fill>();
  definer.define<QImage_Formats>();
  definer.define<QImage_PixelToColor>();
  definer.define<QImage_ColorToPixel>();
}

} // namespace QtCollider
