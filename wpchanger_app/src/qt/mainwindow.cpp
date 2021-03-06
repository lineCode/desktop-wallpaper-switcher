#include "mainwindow.h"
#include "aboutdialog/caboutdialog.h"

#include "imagelist/qtimagelistitem.h"
#include "settingsdialog.h"
#include "settings.h"
#include "settings/csettings.h"
#include "system/ctimeelapsed.h"

DISABLE_COMPILER_WARNINGS
#include "ui_mainwindow.h"

#include <QFileDialog>
#include <QTreeView>
#include <QUrl>
#include <QFile>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QMessageBox>
#include <QtAlgorithms>
#include <QDebug>
#include <QMimeData>
#include <QStringBuilder>
#include <QHeaderView>
#include <QDesktopServices>
#include <QStandardPaths>
RESTORE_COMPILER_WARNINGS

#include <thread>

#ifdef WIN32
#include <windows.h>
#pragma comment(lib, "user32.lib")
#endif

MainWindow::MainWindow(QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::MainWindow),
	_trayIcon(QApplication::style()->standardIcon(QStyle::SP_MediaStop), this),
	_wpChanger(WallpaperChanger::instance()),
	_timeToSwitch(0),
	_bListSaved(true),
	_previousListSize(0)
{
	ui->setupUi(this);

	_trayIcon.setParent(this);
	_trayIcon.show();
	connect(&_trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));


	ui->_imageList->sortByColumn(FileNameColumn, Qt::AscendingOrder);

	ui->statusBar->addWidget(&_statusBarMsgLabel);

	_statusBarTimeToSwitchLabel.setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	_statusBarTimeToSwitchLabel.setToolTip("Time until next switch");
	ui->statusBar->addWidget(&_statusBarTimeToSwitchLabel);


	ui->statusBar->addWidget(&_statusBarNumImages);

	connect(ui->actionAdd_Images,SIGNAL(triggered()), SLOT(onAddImagesTriggered()));
	connect(ui->_imageList, SIGNAL(currentItemChanged(QTreeWidgetItem*,QTreeWidgetItem*)), SLOT(onImgSelected(QTreeWidgetItem*,QTreeWidgetItem*)));
	connect(ui->_imageList, SIGNAL(doubleClicked(QModelIndex)), SLOT(onWPDblClick(QModelIndex)));
	connect(ui->actionSave_Image_List, SIGNAL(triggered()), SLOT(saveImageList()));
	connect(ui->actionSave_Image_List_As, SIGNAL(triggered()), SLOT(saveImageListAs()));
	connect(ui->actionLoad_Image_List, SIGNAL(triggered()), SLOT(loadImageList()));
	connect(ui->_wpModeComboBox, SIGNAL(activated(int)), SLOT(displayModeChanged(int)));
	connect(ui->actionBrowser, SIGNAL(triggered()), SLOT(openImageBrowser()));
	connect(ui->actionSettings, SIGNAL(triggered()), SLOT(openSettings()));
	connect(ui->actionSearch_images_by_file_name, SIGNAL(triggered()), SLOT(search()));
	connect(ui->actionFind_duplicate_files_on_disk, SIGNAL(triggered()), SLOT(findDuplicateFiles()));
	connect(ui->actionFind_duplicate_list_entries, SIGNAL(triggered()), SLOT(selectDuplicateEntries()));
	connect(ui->actionRemove_Non_Existent_Entries, SIGNAL(triggered()), SLOT(removeNonExistingEntries()));
	connect(ui->actionExit, SIGNAL(triggered()), qApp, SLOT(quit()));
	connect(ui->action_About, SIGNAL(triggered()), SLOT(onActionAboutTriggered()));

	connect(ui->actionPrevious_Wallpaper, SIGNAL(triggered()), SLOT(previousWallpaper()));
	connect(ui->actionNext_wallpaper, SIGNAL(triggered()), SLOT(nextWallpaper()));
	connect(ui->actionStop_Switching, SIGNAL(triggered()), SLOT(stopSwitching()));
	connect(ui->actionStart_switching, SIGNAL(triggered()), SLOT(resumeSwitching()));

	connect(ui->actionRemove_Current_Image_from_List, SIGNAL(triggered()), SLOT(removeCurrentWp()));
	connect(ui->actionDelete_Current_Wallpaper_From_Disk, SIGNAL(triggered()), SLOT(deleteCurrentWp()));
	connect(ui->actionDelete_Current_Wallpaper_And_Switch_To_Next, &QAction::triggered, this, &MainWindow::deleteCurrentAndSwitchToNext);

	ui->actionStart_switching->setChecked(CSettings().value(SETTINGS_START_SWITCHING_ON_STARTUP, SETTINGS_DEFAULT_AUTOSTART).toBool());

	_imageListFilterDialog.setParent(ui->_imageList);
	_imageListFilterDialog.hide();
	connect(&_imageListFilterDialog, SIGNAL(filterTextChanged(QString)), SLOT(searchByFilename(QString)));

	setAcceptDrops(true);

	_wpChanger.addSubscriber(this);
	timeToNextSwitch(_wpChanger.interval());

	connect(ui->_imageList, SIGNAL(customContextMenuRequested(const QPoint&)), SLOT(showImageListContextMenu(const QPoint&)));

	const QString listFileName(CSettings().value(SETTINGS_IMAGE_LIST_FILE).toString());
	if (!listFileName.isEmpty())
	{
		if (_wpChanger.loadList(listFileName))
		{
			_currentListFileName = listFileName;
			if (CSettings().value(SETTINGS_START_SWITCHING_ON_STARTUP, SETTINGS_DEFAULT_AUTOSTART).toBool())
				_wpChanger.startSwitching();

			updateWindowTitle();
		}
	}
	else
		_statusBarMsgLabel.setText("Start by dragging and dropping images");

	const size_t wpIndex = (size_t)CSettings().value(SETTINGS_CURRENT_WALLPAPER, std::numeric_limits<uint>().max()).toUInt();
	if (wpIndex < std::numeric_limits<uint>().max())
		_wpChanger.setCurrentWpIndex(wpIndex);

	_previousListSize = _wpChanger.numImages();

	connect(this, SIGNAL(signalUpdateProgress(int,bool,QString)), SLOT(updateProgress(int,bool,QString)), Qt::QueuedConnection);

	_progressBar.setVisible(false);
	ui->statusBar->addWidget(&_progressBar);
}

MainWindow::~MainWindow()
{
	delete ui;
}

//Displays a given message in the status bar
void MainWindow::setStatusBarMessage( const QString& msg )
{
	_statusBarMsgLabel.setText(msg);
}

//Updates the contents of image list control according to _wpChanger::_imageList
void MainWindow::updateImageList(bool totalUpdate)
{
	disconnect(ui->_imageList, SIGNAL(currentItemChanged(QTreeWidgetItem*,QTreeWidgetItem*)), this, SLOT(onImgSelected(QTreeWidgetItem*,QTreeWidgetItem*)));
	CTimeElapsed stopWatch(true);

	if (_wpChanger.numImages() > 0 && _previousListSize != 0)
	{
		if (_bListSaved && _previousListSize != _wpChanger.numImages())
		{
			_bListSaved = false;
			updateWindowTitle();
		}
		_previousListSize = _wpChanger.numImages();
	}

	if (totalUpdate)
	{
		// Cleaning up the whole list of QTreeWidget items
		for (auto it = _imageListWidgetItems.begin(); it != _imageListWidgetItems.end(); ++it)
		{
			delete it->second; // This both deletes the item and removes it from the widget: http://qt-project.org/doc/qt-4.8/qtreewidgetitem.html#dtor.QTreeWidgetItem
		}
		_imageListWidgetItems.clear();
	}

	decltype(_imageListWidgetItems) newImageListWidgetItems;
	for (size_t i = 0; i < _wpChanger.numImages(); ++i)
	{
		const qulonglong id = _wpChanger.image(i).id();
		const auto imageListItem = _imageListWidgetItems.find(id);
		if (imageListItem == _imageListWidgetItems.end()) // This image is not yet in the widget, adding
		{
			QtImageListItem * item =  new QtImageListItem(_wpChanger.image(i), _wpChanger.currentWallpaper() == i);
			if (!_wpChanger.imageExists(i))
			{
				for (int coulmn = 0; coulmn < ui->_imageList->columnCount(); ++coulmn)
				{
					item->setBackgroundColor(coulmn, QColor(Qt::red));
					item->setTextColor(coulmn, QColor(Qt::white));
				}
			}

			newImageListWidgetItems[id] = item;
			ui->_imageList->addTopLevelItem(item);
		}
		else // An entry for this image has already been added, no need to re-create it
		{
			imageListItem->second->setCurrent(false);
			newImageListWidgetItems[imageListItem->first] = imageListItem->second;
			_imageListWidgetItems.erase(imageListItem);
		}
	}

	// Cleaning up entries for no longer existing images
	for (auto it = _imageListWidgetItems.begin(); it != _imageListWidgetItems.end(); ++it)
	{
		delete it->second; // This both deletes the item and removes it from the widget: http://qt-project.org/doc/qt-4.8/qtreewidgetitem.html#dtor.QTreeWidgetItem
	}

	// Marking the current WP
	assert_r(_wpChanger.currentWallpaper() == invalid_index || newImageListWidgetItems.count(_wpChanger.image(_wpChanger.currentWallpaper()).id()) > 0);
	if (_wpChanger.currentWallpaper() != invalid_index)
		newImageListWidgetItems[_wpChanger.image(_wpChanger.currentWallpaper()).id()]->setCurrent();

	// Updating the list
	_imageListWidgetItems = newImageListWidgetItems;

	const int topLevelItemCount = ui->_imageList->topLevelItemCount();
	assert_r(_imageListWidgetItems.size() == (size_t)topLevelItemCount);

	for (int column = 0; column < ui->_imageList->columnCount(); ++column)
	{
		ui->_imageList->resizeColumnToContents(column);
	}

	connect(ui->_imageList, SIGNAL(currentItemChanged(QTreeWidgetItem*,QTreeWidgetItem*)), this, SLOT(onImgSelected(QTreeWidgetItem*,QTreeWidgetItem*)));
	_statusBarNumImages.setText(QString ("%1 images in the list").arg(_wpChanger.numImages()));
	qDebug() << "Updating list of " << _wpChanger.numImages() << " items took " << stopWatch.elapsed() / 1000.0f <<"sec";
}

void MainWindow::addImagesFromDirecoryRecursively(const QString& path)
{
	const QFileInfo info(path);
	if (info.exists())
	{
		if (info.isDir())
		{
			const QDir dir(path);
			const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
			for (int i = 0; i < entries.size(); ++i)
			{
				addImagesFromDirecoryRecursively(entries[i].absoluteFilePath());
			}
		}
		else if (info.isFile() && WallpaperChanger::isSupportedImageFile(path))
		{
			//Attempt to add and indicate the result
			if (!_wpChanger.addImage(path))
				setStatusBarMessage(path + " : " + "failed to open as image");
		}
	}
}

void MainWindow::promptToSaveList()
{
	if (QMessageBox::question(this, "Save changes?", "The image list was modified, do you want to save changes?", QMessageBox::Save | QMessageBox::No) == QMessageBox::Save)
		saveImageList();
}

//Deletes all the items of the tree, freeing the memory
void MainWindow::clearImageList()
{
	//For all top-level items
	for (int i = 0; i < ui->_imageList->topLevelItemCount(); ++i)
	{
		QTreeWidgetItem* item = ui->_imageList->topLevelItem(i);
		//Deleting all child items
		for (int k = 0; k < item->childCount(); ++k)
		{
			if (item->child(k))
				delete item->child(k);
		}
		//Deleting the current top-level item
		delete item;
	}
	ui->_imageList->clear();
}

void MainWindow::loadGeometry()
{
	CSettings s;
	restoreGeometry( s.value(SETTINGS_WINDOW_GEOMETRY).toByteArray());
	restoreState( s.value(SETTINGS_WINDOW_STATE).toByteArray());
	ui->_imageList->header()->restoreGeometry(s.value(SETTINGS_LIST_GEOMETRY).toByteArray());
	ui->_imageList->header()->restoreState(s.value(SETTINGS_LIST_STATE).toByteArray());
}

bool MainWindow::event(QEvent * event)
{
	static bool firstShown = true;
	if(event->type() == QEvent::WindowStateChange && isMinimized())
		QTimer::singleShot(0, this, SLOT(hide()));
	else if (event->type() == QEvent::Show && firstShown)
	{
		firstShown = false;
		QTimer::singleShot(0, this, SLOT(hide()));
	}

	return QWidget::event(event);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
	if (e->type() == QCloseEvent::Close)
	{
		CSettings s;
		s.setValue(SETTINGS_WINDOW_GEOMETRY, saveGeometry());
		s.setValue(SETTINGS_WINDOW_STATE, saveState());
		s.setValue(SETTINGS_LIST_GEOMETRY, ui->_imageList->header()->saveGeometry());
		s.setValue(SETTINGS_LIST_STATE, ui->_imageList->header()->saveState());

		if (!_bListSaved)
			promptToSaveList();
	}
}

// Launch the search UI
void MainWindow::search()
{
	_imageListFilterDialog.display();
}

// Search the list by given filename pattern
void MainWindow::searchByFilename(QString name)
{
	if (!name.isEmpty())
	{
		if (!name.startsWith("*"))
			name.prepend("*");
		if (!name.endsWith("*"))
			name.append("*");

		const auto matchingItems = ui->_imageList->findItems(name, Qt::MatchWildcard, FileNameColumn);

		for (int i = 0; i < ui->_imageList->topLevelItemCount(); ++i)
			ui->_imageList->topLevelItem(i)->setHidden(true);

		for (auto& item : matchingItems)
			item->setHidden(false);
	}
	else
	{
		for (int i = 0; i < ui->_imageList->topLevelItemCount(); ++i)
			ui->_imageList->topLevelItem(i)->setHidden(false);
	}
}

// Select duplicate entries in the list
void MainWindow::selectDuplicateEntries()
{
	struct ImagePath {
		ImagePath(const QString path_, qulonglong id_): path(path_), id(id_) {}
		bool operator<(const ImagePath& other) const {return path < other.path;}
		bool operator==(const ImagePath& other) const {return path == other.path;}

		QString    path;
		qulonglong id;
	};

	std::vector<ImagePath> imagePaths;
	for (size_t i = 0; i < _wpChanger.numImages(); ++i)
		imagePaths.emplace_back(ImagePath(_wpChanger.image(i).imageFilePath(), _wpChanger.image(i).id()));

	std::sort(imagePaths.begin(), imagePaths.end());

	auto it = std::adjacent_find(imagePaths.begin(), imagePaths.end());
	while(it != imagePaths.end())
	{
		selectImage(it->id);
		it = std::adjacent_find(it+1, imagePaths.end());
	}
}

// Find and select duplicate files on disk
void MainWindow::findDuplicateFiles()
{
	std::thread([this]() {
		struct ImageContentsHash {
			ImageContentsHash(qulonglong contentsHash_, qulonglong id_): contentsHash(contentsHash_), id(id_) {}
			bool operator<(const ImageContentsHash& other) const {return contentsHash < other.contentsHash;}
			bool operator==(const ImageContentsHash& other) const {return contentsHash == other.contentsHash;}

			qulonglong contentsHash;
			qulonglong id;
		};

		emit signalUpdateProgress(0, true, QString("Scanning files (0/%1)...").arg(_wpChanger.numImages()));
		std::vector<ImageContentsHash> imageHashes;
		for (size_t i = 0; i < _wpChanger.numImages(); ++i)
			if (_wpChanger.imageExists(i))
			{
				imageHashes.emplace_back(ImageContentsHash(_wpChanger.image(i).contentsHash(), _wpChanger.image(i).id()));
				emit signalUpdateProgress(100 * i / _wpChanger.numImages(), true, QString("Scanning files (%1/%2)...").arg(i).arg(_wpChanger.numImages()));
			}

			std::sort(imageHashes.begin(), imageHashes.end());

			auto it = std::adjacent_find(imageHashes.begin(), imageHashes.end());
			while(it != imageHashes.end())
			{
				selectImage(it->id);
				it = std::adjacent_find(it+1, imageHashes.end());
			}

			emit signalUpdateProgress(100, false, QString());
	}).detach();
}

void MainWindow::removeNonExistingEntries()
{
	_wpChanger.removeNonexistentEntries();
}

// Removes current wallpaper from list
void MainWindow::removeCurrentWp()
{
	if (_wpChanger.currentWallpaper() != invalid_index)
	{
		_wpChanger.removeImages(std::vector<qulonglong>(1, _wpChanger.idByIndex(_wpChanger.currentWallpaper())));
	}
}

// Deletes current wallpaper from disk
void MainWindow::deleteCurrentWp()
{
	if (_wpChanger.currentWallpaper() != invalid_index)
		_wpChanger.deleteImagesFromDisk(std::vector<qulonglong>(1, _wpChanger.idByIndex(_wpChanger.currentWallpaper())));
}

void MainWindow::deleteCurrentAndSwitchToNext()
{
	deleteCurrentWp();
	nextWallpaper();
}

//Request to show "About" window
void MainWindow::onActionAboutTriggered()
{
	CAboutDialog(this).exec();
}

//Triggers a dialog window to add images to a list
void MainWindow::onAddImagesTriggered()
{
	const QString location = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);

	QStringList images = QFileDialog::getOpenFileNames(
		this,
		"Select one or more images to add",
		location,
		"Images (*.png *.bmp *.jpg *.jpeg *.gif)");

	if (! (images.empty() ))
	{
		CTimeElapsed stopWatch(true);
		QStringList::const_iterator it = images.begin();
		for (; it != images.end(); ++it)
		{
			if (WallpaperChanger::isSupportedImageFile(*it))
				if (!_wpChanger.addImage(*it))
					setStatusBarMessage(*it + " : " + "failed to open as image");
		}
		ui->ImageThumbWidget->displayImage(images.back());
		ui->ImageThumbWidget->update();

		qDebug() << "Opening " << images.size() << "items took " << stopWatch.elapsed() / 1000.0f <<"secs";
	}
}

void MainWindow::onImgSelected(QTreeWidgetItem* current, QTreeWidgetItem* /*prev*/)
{
	if (!current)
		return;

	const size_t currentlySelectedItemIndex = _wpChanger.indexByID(current->data(0, IdRole).toULongLong());
	if (currentlySelectedItemIndex < _wpChanger.numImages())
	{
		ui->ImageThumbWidget->displayImage(_wpChanger.image(currentlySelectedItemIndex));
		displayImageInfo(currentlySelectedItemIndex);
	}
}

void MainWindow::dropEvent(QDropEvent * de)
{
	CTimeElapsed stopWatch(true);
	const QMimeData * mimeData = de->mimeData();
	_wpChanger.enableListUpdateCallbacks(false);

	//For every dropped file
	for (int urlIndex = 0; urlIndex < mimeData->urls().size(); ++urlIndex)
	{
		//Remove the heading '/
		const QString filename = mimeData->urls().at(urlIndex).path().remove(0, 1);
		// Checking for WP list
		if (mimeData->urls().size() == 1 && mimeData->urls().at(0).path().remove(0, 1).endsWith(".wil"))
		{
			if (_wpChanger.loadList(mimeData->urls().at(0).path()))
			{
				_currentListFileName = mimeData->urls().at(0).path();
				CSettings().setValue(SETTINGS_IMAGE_LIST_FILE, _currentListFileName);
				updateWindowTitle();
			}

			break;
		}
		else
			addImagesFromDirecoryRecursively(filename);
	}

	_wpChanger.enableListUpdateCallbacks(true);
	updateImageList(false);

	qDebug() << "Dropping " << mimeData->urls().size() << "Items took " << stopWatch.elapsed() / 1000.0f <<"secs";
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
	if (event->mimeData()->hasUrls())
	{
		event->acceptProposedAction();
	}
}

void MainWindow::onWPDblClick( QModelIndex )
{
	const QTreeWidgetItem* item = ui->_imageList->currentItem();
	const size_t idx = _wpChanger.indexByID(item->data(0, IdRole).toULongLong());
	if ( !_wpChanger.setWallpaper(idx) )
	{
		setStatusBarMessage("Failed to set selected picture as a wallpaper");
	}
}

//Wallpaper display mode changed
void MainWindow::displayModeChanged (int mode)
{
	assert_r(mode >= 0 && mode <= SYSTEM_DEFAULT);

	_bListSaved = false;
	QList<QTreeWidgetItem*> selected = ui->_imageList->selectedItems();
	const int numSelected = selected.size();
	for (int i = 0; i < numSelected; ++i)
	{
		const size_t itemIdx = (size_t)_wpChanger.indexByID(selected[i]->data(0, IdRole).toULongLong());
		_wpChanger.image(itemIdx).setStretchMode(WPOPTIONS(mode));
	}

	updateImageList(true);
	updateWindowTitle();
}

// Image browser requested
void MainWindow::openImageBrowser()
{
	_browserWindow.showMaximized();
}

void MainWindow::showImageListContextMenu(const QPoint& pos)
{
	if (ui->_imageList->selectedItems().isEmpty())
		return;

	const QPoint globalPos = ui->_imageList->viewport()->mapToGlobal(pos);

	QMenu menu;
	QAction * deleteFromDisk = menu.addAction("Delete from disk");
	QAction * view           = menu.addAction("View");
	QAction * openFolder     = menu.addAction("Open folder for browsing");

	QAction * selectedItem = menu.exec(globalPos);
	if (selectedItem == deleteFromDisk)
	{
		if (QMessageBox::question(this, "Are you sure?", "Are you sure you want to irreversibly delete the selected image(s) from disk?", QMessageBox::Ok | QMessageBox::Cancel) == QMessageBox::Ok)
			deleteSelectedImagesFromDisk();
	}
	else if (selectedItem == view)
	{
		if (ui->_imageList->currentItem())
			QDesktopServices::openUrl(QUrl::fromLocalFile(_wpChanger.image(_wpChanger.indexByID(ui->_imageList->currentItem()->data(0, IdRole).toULongLong())).imageFilePath()));
	}
	else if (selectedItem == openFolder)
	{
		if (ui->_imageList->currentItem())
			QDesktopServices::openUrl(QUrl::fromLocalFile(_wpChanger.image(_wpChanger.indexByID(ui->_imageList->currentItem()->data(0, IdRole).toULongLong())).imageFileFolder()));
	}
	else if (selectedItem)
	{
		assert_unconditional_r("Unhandled menu item activated");
	}
}

void MainWindow::openSettings()
{
	SettingsDialog().exec();
}

void MainWindow::displayImageInfo (size_t imageIndex)
{
	const Image& img = _wpChanger.image(imageIndex);
	ui->_filePathIndicator->setText(img.imageFileFolder());
	ui->_imageDimensionsIndicatorLabel->setText(QString("%1x%2").arg(img.params()._width).arg(img.params()._height));
	ui->_imageSizeIndicatorLabel->setText(QString("%1 KB").arg(img.params()._fileSize / 1024));
	ui->_wpModeComboBox->setCurrentIndex(img.params()._wpDisplayMode);
	ui->_wpModeComboBox->setEnabled(true);
}

void MainWindow::saveImageListAs()
{
	_currentListFileName = QFileDialog::getSaveFileName(this, "Choose the file you want to save the image list to", QString(), "WallpaperChanger image list file (*.wil)");
	if (_currentListFileName.isEmpty())
		return;

	if (!_currentListFileName.contains(".wil"))
		_currentListFileName.append(".wil");

	if (!_wpChanger.saveList(_currentListFileName))
	{
		QMessageBox::information(this, "Error saving image list", "Error occurred while trying to save image list. Try saving to another file or folder.");
	}
	else
	{
		_bListSaved = true;
		CSettings().setValue(SETTINGS_IMAGE_LIST_FILE, _currentListFileName);
		updateWindowTitle();
	}
}

void MainWindow::saveImageList()
{
	if (_currentListFileName.isEmpty())
	{
		saveImageListAs();
	}
	else if (!_wpChanger.saveList(_currentListFileName))
	{
		QMessageBox::information(this, "Error saving image list!", "Error occurred while trying to save image list. Try saving to another file or folder.");
	}
	else
	{
		_bListSaved = true;
		updateWindowTitle();
	}
}

void MainWindow::loadImageList()
{
	if (!_bListSaved)
		promptToSaveList();

	const QString filename = QFileDialog::getOpenFileName(this, "Choose the file to load image list from", QString(), "WallpaperChanger image list file (*.wil)");
	if (!filename.isEmpty())
	{
		setStatusBarMessage("");
		if (!_wpChanger.loadList(filename))
		{
			QMessageBox::information(this, "Error loading image list", "Error occurred while trying to load image list. The file is either inaccessible or corrupt.");
		}
		else
		{
			_currentListFileName = filename;
			_bListSaved = true;
			_previousListSize = _wpChanger.numImages();
			CSettings().setValue(SETTINGS_IMAGE_LIST_FILE, _currentListFileName);
			updateWindowTitle();
		}
	}
}

// Switching
void MainWindow::nextWallpaper()
{
	_wpChanger.nextWallpaper();
}

void MainWindow::previousWallpaper()
{
	_wpChanger.previousWallpaper();
}

void MainWindow::stopSwitching()
{
	_wpChanger.stopSwitching();
}

void MainWindow::resumeSwitching()
{
	_wpChanger.startSwitching();
}

void MainWindow::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
	if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::MiddleClick || reason == QSystemTrayIcon::Trigger)
		restoreWindow();
	else if (reason == QSystemTrayIcon::Context)
	{
		QMenu menu;
		menu.addAction(ui->actionNext_wallpaper);
		menu.addAction(ui->actionPrevious_Wallpaper);
		menu.addSeparator();
		menu.addAction(ui->actionDelete_Current_Wallpaper_From_Disk);
		menu.addSeparator();
		menu.addAction(ui->actionExit);

		menu.exec(_trayIcon.geometry().topLeft());
	}
}

void MainWindow::restoreWindow()
{
	show();
	setWindowState( (windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
	raise();
	activateWindow();
}

//Key pressed
void MainWindow::keyPressEvent(QKeyEvent *e)
{
	if (e->type() == QKeyEvent::KeyPress && ui->_imageList->hasFocus())
	{
		switch (e->key())
		{
		case Qt::Key_Delete: removeSelectedImages();
			break;
		default:
			QMainWindow::keyPressEvent(e);
		}
	}
}

void MainWindow::updateProgress(int percent, bool show, QString text)
{
	_progressBar.setValue(percent);
	_progressBar.setVisible(show);
	_progressBar.setFormat(text + " %p%");
}

void MainWindow::selectImage(qulonglong id)
{
	for (int i = 0; i < ui->_imageList->topLevelItemCount(); ++i)
		if (ui->_imageList->topLevelItem(i)->data(0, IdRole).toULongLong() == id)
		{
			ui->_imageList->topLevelItem(i)->setSelected(true);
		}
}

void MainWindow::removeSelectedImages()
{
	QList<QTreeWidgetItem*> selected(ui->_imageList->selectedItems());
	const int numSelected = selected.size();
	if (numSelected <= 0)
		return;

	std::vector<qulonglong> idsToRemove;
	for (int i = 0; i < numSelected; ++i)
	{
		const qulonglong id = selected[i]->data(0, IdRole).toULongLong();
		idsToRemove.push_back(id);
	}

	_wpChanger.removeImages(idsToRemove);
	_bListSaved = false;
	updateWindowTitle();
}

//Delete selected images from disk (and from the list if successful)
void MainWindow::deleteSelectedImagesFromDisk()
{
	QList<QTreeWidgetItem*> selected = ui->_imageList->selectedItems();
	const int numSelected = selected.size();
	if (numSelected <= 0)
		return;

	std::vector<qulonglong> idsToRemove;
	for (int i = 0; i < numSelected; ++i)
	{
		idsToRemove.push_back(selected[i]->data(0, IdRole).toULongLong());
	}

	_wpChanger.deleteImagesFromDisk(idsToRemove);
	_bListSaved = false;
	updateWindowTitle();
}

void MainWindow::updateWindowTitle()
{
	QString title = "Wallpaper Changer";
	if (!_currentListFileName.isEmpty())
	{
		if (!_bListSaved)
			title.prepend("* ");
		title = title % " - " % QFileInfo(_currentListFileName).fileName();
	}

	setWindowTitle(title);
}

void MainWindow::listChanged(size_t /*index*/)
{
	updateImageList(false);
}

// Image list was cleared
void MainWindow::listCleared()
{
	updateImageList(true);
}

void MainWindow::wallpaperChanged(size_t index)
{
	CSettings().setValue(SETTINGS_CURRENT_WALLPAPER, (uint)index);
	_trayIcon.setToolTip(_wpChanger.image(index).imageFileName());
	for (int i = 0; i < ui->_imageList->topLevelItemCount(); ++i)
	{
		QtImageListItem * item = dynamic_cast<QtImageListItem*>(ui->_imageList->topLevelItem(i));
		if (!item)
			continue;

		if (index < invalid_index && _wpChanger.image(index).id() == item->data(0, IdRole).toULongLong())
		{
			item->setCurrent();
		}
		else
			item->setCurrent(false);
	}

	ui->_imageList->resizeColumnToContents(MarkerColumn);
}

void MainWindow::timeToNextSwitch(size_t seconds)
{
	_timeToSwitch = seconds;
	const int sec = seconds % 60;
	seconds -= sec;
	const int min = ( seconds / 60 ) % 60;
	seconds -= min * 60;
	const int hr = int (seconds / 3600);
	_statusBarTimeToSwitchLabel.setText(QString("%1:%2:%3").arg(hr, 2, 10, QChar('0') ).arg(min, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0')));
}

void MainWindow::wallpaperAdded(size_t)
{
}
