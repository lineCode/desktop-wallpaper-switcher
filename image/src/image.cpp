#include "image.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QCryptographicHash>
#include <QUuid>
#include <QFile>
#include <QDebug>
RESTORE_COMPILER_WARNINGS

#include <assert.h>

Image::Image() :_id(0u), _isValid(false)
{
}

Image::Image( const QString& filename, ImgParams params /* = ImgParams() */) :
	_params(params)
{
	loadFromFile(filename);
}

bool Image::isValidImage() const
{
	return _isValid;
}

bool Image::loadFromFile( const QString& filename )
{
	_filePath = filename;
	QFileInfo info (filename);
	_name = info.fileName();
	_folder = info.dir().path();

	if (_params != ImgParams())
	{
		_isValid = true;
	}
	else
	{
		QImageReader reader (filename);
		_isValid = reader.canRead();
		if (_isValid)
		{
			_params._width = reader.size().width();
			_params._height = reader.size().height();

			//Determine the image format by file extension
			const QString extension = info.suffix().toLower();
			if (extension == "jpg" || extension == "jpeg")
				_params._fmt = JPG;
			else if (extension == "bmp")
				_params._fmt = BMP;
			else if (extension == "gif")
				_params._fmt = GIF;
			else if (extension == "jpg")
				_params._fmt = JPG;
			else if (extension == "tiff")
				_params._fmt = TIFF;
			else if (extension == "xbm")
				_params._fmt = XBM;
			else if (extension == "xpm")
				_params._fmt = XPM;
			else if (extension == "png")
				_params._fmt = PNG;

			_params._fileSize = (int)info.size();
		}
	}

	const QByteArray uniqueData = _filePath.toUtf8().append(QUuid::createUuid().toByteArray());
	const QByteArray hash = QCryptographicHash::hash(uniqueData, QCryptographicHash::Md5);
	assert(hash.size() == 16);

	_id = *(qulonglong*)(hash.data()) ^ *(qulonglong*)(hash.data()+8);
	return _isValid;
}

const QString& Image::imageFilePath() const
{
	return _filePath;
}

const QString& Image::imageFileFolder() const
{
	return _folder;
}

const QString& Image::imageFileName() const
{
	return _name;
}

QImage Image::constructQImageObject() const
{
	QImage qImg;
	if (_isValid)
	{
		qImg.load(_filePath);
	}

	return qImg;
}

const ImgParams& Image::params() const
{
	return _params;
}

WPOPTIONS Image::stretchMode () const
{
	return _params._wpDisplayMode;
}

void Image::setStretchMode( WPOPTIONS mode ) const
{
	_params._wpDisplayMode = mode;
}

qulonglong Image::id() const
{
	return _id;
}

qulonglong Image::contentsHash() const
{
	QFile imageFile(_filePath);
	if (!imageFile.exists())
		return 0;

	if (!imageFile.open(QIODevice::ReadOnly))
	{
		qDebug() << "Couldn't open file" << _filePath << ":" << imageFile.errorString();
		return 0;
	}

	const QByteArray hash(QCryptographicHash::hash(imageFile.readAll(), QCryptographicHash::Md5));
	assert(hash.size() == 16);

	return *(qulonglong*)(hash.data()) ^ *(qulonglong*)(hash.data()+8);
}
