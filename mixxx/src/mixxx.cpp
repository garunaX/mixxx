/***************************************************************************
                          mixxx.cpp  -  description
                             -------------------
    begin                : Mon Feb 18 09:48:17 CET 2002
    copyright            : (C) 2002 by Tue and Ken Haste Andersen
    email                :
***************************************************************************/

/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/


#include <QtDebug>
// #include <QMessageBox>
// #include <QLayout>
// #include <qdesktopwidget.h>
// #include <qicon.h>
// #include <qsplashscreen.h>
// #include <qdatetime.h>
// #include <qpalette.h>
// #include <QFileDialog>
// #include <QFrame>

#include "wknob.h"
#include "wslider.h"
#include "wpushbutton.h"
#include "woverview.h"
#include "mixxx.h"
#include "controlnull.h"
#include "readerextractwave.h"
#include "controlpotmeter.h"
#include "reader.h"
#include "enginebuffer.h"
#include "enginevumeter.h"
#include "track.h"
#include "trackcollection.h"
#include "trackinfoobject.h"
#include "mixxxsocketserver.h"
#include "mixxxmenuplaylists.h"
#include "wavesummary.h"
#include "bpmdetector.h"
#include "log.h"
#include "dlgabout.h"

#include "playerproxy.h"
#include "soundmanager.h"
#include "defs_urls.h"

#ifdef __C_METRICS__
#include <cmetrics.h>
#include "defs_mixxxcmetrics.h"
#endif

MixxxApp::MixxxApp(QApplication * a, struct CmdlineArgs args, QSplashScreen * pSplash)
{
    app = a;

    qDebug("Starting up...");
    setWindowTitle(tr("Mixxx " VERSION));
#ifdef __MACX__
    setWindowIcon(QIcon(":icon.svg"));
#else
    setWindowIcon(QIcon(":icon.svg"));
    //setWindowIcon(QIcon(":iconsmall.png")); //This is a smaller 16x16 icon, looks cleaner...
#endif

    //Reset pointer to players
    //player = 0;
    soundmanager = 0;
    m_pTrack = 0;
    prefDlg = 0;

    // Read the config file from home directory
    config = new ConfigObject<ConfigValue>(QDir::homePath().append("/").append(SETTINGS_FILE));
    QString qConfigPath = config->getConfigPath();

#ifdef __C_METRICS__
	//Initialize Case Metrics if User is OK with that
	int fuserAgreeToDataCollection = false;
	if(config->getValueString(ConfigKey("[User Experience]","AgreedToUserExperienceProgram")) == QString("yes"))
		fuserAgreeToDataCollection = true;
	else if(config->getValueString(ConfigKey("[User Experience]","AgreedToUserExperienceProgram")) != QString("no"))
	{
		while(1)
		{
			fuserAgreeToDataCollection = QMessageBox::question(this, "Mixxx", "Mixxx's development is driven by community feedback.  At your discretion, Mixxx can automatically send data on your user experience back to the developers.\n\nWould you like to enable this feature?", "Yes", "No", "Privacy Policy", 0, -1);
			if(fuserAgreeToDataCollection == 0)
			{
				fuserAgreeToDataCollection = true;
				config->set(ConfigKey("[User Experience]","AgreedToUserExperienceProgram"), ConfigValue(QString("yes")));
				break;
			}
			else if(fuserAgreeToDataCollection == 1)
			{
				fuserAgreeToDataCollection = false;
				config->set(ConfigKey("[User Experience]","AgreedToUserExperienceProgram"), ConfigValue(QString("no")));
				break;
			}
			else
			{
				//show privacy policy
				QMessageBox::information(this, "Mixxx: Privacy Policy", "Mixxx's development is driven by community feedback.  In order to help improve future versions Mixxx will with your permission collect information on your hardware and usage of Mixxx.  This information will primarily be used to fix bugs, improve features, and determine the system requirements of later versions.  Additionally this information may be used in aggregate for statistical purposes.\n\nThe hardware information will include:\n\t- CPU model and features\n\t- Total/Available Amount of RAM\n\t- Available disk space\n\t- OS version\n\nYour usage information will include:\n\t- Settings/Preferences\n\t- Internal errors\n\t- Internal debugging messages\n\t- Performance statistics (average latency, CPU usage)\n\nThis information will not be used to personally identify you, contact you, advertise to you, or otherwise bother you in any way.\n");
			}
		}
	}
	cm_init(100,20, fuserAgreeToDataCollection);
	cm_writemsg_ascii(MIXXXCMETRICS_VERSION,
	                  VERSION);
#endif

    // Store the path in the config database
    config->set(ConfigKey("[Config]","Path"), ConfigValue(qConfigPath));

    // Instantiate a ControlObject, and set static parent widget
    control = new ControlNull();


    if (pSplash)
        pSplash->showMessage("Initializing control devices...",Qt::AlignLeft|Qt::AlignBottom);

    // Read keyboard configuration and set kdbConfig object in WWidget
    kbdconfig = new ConfigObject<ConfigValueKbd>(QString(qConfigPath).append("keyboard/").append("Standard.kbd.cfg"));
    WWidget::setKeyboardConfig(kbdconfig);


    if (pSplash)
        pSplash->showMessage("Setting up sound engine...",Qt::AlignLeft|Qt::AlignBottom);

    // Sample rate used by Player object
    ControlObject * sr = new ControlObject(ConfigKey("[Master]","samplerate"));
    sr->set(44100.);

    ControlObject * latency = new ControlObject(ConfigKey("[Master]","latency"));
    /* avoid unused warning*/
    latency = 0;

    // Master rate
    new ControlPotmeter(ConfigKey("[Master]","rate"),-1.,1.);

    // Init buffers/readers
    buffer1 = new EngineBuffer("[Channel1]", config);
    buffer2 = new EngineBuffer("[Channel2]", config);
    buffer1->setOtherEngineBuffer(buffer2);
    buffer2->setOtherEngineBuffer(buffer1);

    // Starting channels:
    channel1 = new EngineChannel("[Channel1]");
    channel2 = new EngineChannel("[Channel2]");

    // Starting the master (mixing of the channels and effects):
    master = new EngineMaster(config, buffer1, buffer2, channel1, channel2, "[Master]");

    // Initialize player device
    //Player::setMaster(master);
    //player = new PlayerProxy(config);

    soundmanager = new SoundManager(config, master);
    soundmanager->queryDevices();

    if (pSplash)
        pSplash->showMessage("Loading skin...",Qt::AlignLeft|Qt::AlignBottom);

    // Find path of skin
    QString qSkinPath = getSkinPath();

    // Get Music dir
    QDir dir(config->getValueString(ConfigKey("[Playlist]","Directory")));
    if ((config->getValueString(ConfigKey("[Playlist]","Directory")).length()<1) || (!dir.exists()))
    {
        QString fd = QFileDialog::getExistingDirectory(this, "Choose music library directory");
        if (fd != "")
        {
            config->set(ConfigKey("[Playlist]","Directory"), fd);
            config->Save();
        }
    }
    // Needed for Search class and Simple skin
    new ControlPotmeter(ConfigKey("[Channel1]","virtualplayposition"),0.,1.);

    // Needed for updating widgets with track duration info
    new ControlObject(ConfigKey("[Channel1]","duration"));
    new ControlObject(ConfigKey("[Channel2]","duration"));

    // Initialize widgets
    bool bVisualsWaveform = true;
    if (config->getValueString(ConfigKey("[Controls]","Visuals")).toInt()==1)
        bVisualsWaveform = false;

    // Use frame as container for view, needed for fullscreen display
    frame = new QFrame;
    setCentralWidget(frame);
    //move(10,10);
    // Call inits to invoke all other construction parts

    view=new MixxxView(frame, kbdconfig, bVisualsWaveform, qSkinPath, config);

    if (bVisualsWaveform && !view->activeWaveform())
    {
        config->set(ConfigKey("[Controls]","Visuals"), ConfigValue(1));
        QMessageBox * mb = new QMessageBox(this);
        mb->setWindowTitle(QString("Wavform displays"));
        mb->setIcon(QMessageBox::Information);
        mb->setText("OpenGL cannot be initialized, which means that\nthe waveform displays won't work. A simple\nmode will be used instead where you can still\nuse the mouse to change speed.");
        mb->show();
    }

    // Tell EngineBuffer to notify the visuals if they are WVisualWaveform
    if (view->activeWaveform())
    {
        buffer1->setVisual((WVisualWaveform *)view->m_pVisualCh1);
        buffer2->setVisual((WVisualWaveform *)view->m_pVisualCh2);

        // Dynamic zoom on visuals
        if (view->m_bZoom)
        {
            ControlObject::connectControls(ConfigKey("[Channel1]", "rate"), ConfigKey("[Channel1]", "VisualLengthScale-marks"));
            ControlObject::connectControls(ConfigKey("[Channel1]", "rate"), ConfigKey("[Channel1]", "VisualLengthScale-signal"));
            ControlObject::connectControls(ConfigKey("[Channel2]", "rate"), ConfigKey("[Channel2]", "VisualLengthScale-marks"));
            ControlObject::connectControls(ConfigKey("[Channel2]", "rate"), ConfigKey("[Channel2]", "VisualLengthScale-signal"));
        }
    }

    // Verify path for xml track file.
    QFile trackfile(config->getValueString(ConfigKey("[Playlist]","Listfile")));
    if ((config->getValueString(ConfigKey("[Playlist]","Listfile")).length()<1) || (!trackfile.exists()))
    {
        config->set(ConfigKey("[Playlist]","Listfile"), QDir::homePath().append("/").append(TRACK_FILE));
        config->Save();
    }

    // Initialize wavefrom summary generation
    m_pWaveSummary = new WaveSummary(config);

    // Initialize Bpm detection queue
    m_pBpmDetector = new BpmDetector(config);

    if (pSplash)
        pSplash->showMessage("Loading song database...",Qt::AlignLeft|Qt::AlignBottom);

    // Initialize track object:
    m_pTrack = new Track(config->getValueString(ConfigKey("[Playlist]","Listfile")), view, buffer1, buffer2, m_pWaveSummary, m_pBpmDetector,config->getValueString(ConfigKey("[Playlist]","Directory")));

    //WTreeItem::setTrack(m_pTrack);
    // Set up drag and drop to player visuals
    if (view->m_pVisualCh1)
        connect(view->m_pVisualCh1, SIGNAL(trackDropped(QString)), m_pTrack, SLOT(slotLoadPlayer1(QString)));
    if (view->m_pVisualCh2)
        connect(view->m_pVisualCh2, SIGNAL(trackDropped(QString)), m_pTrack, SLOT(slotLoadPlayer2(QString)));

    // Ensure that visual receive updates when new tracks are loaded
    if (view->m_pVisualCh1)
        connect(m_pTrack, SIGNAL(newTrackPlayer1(TrackInfoObject *)), view->m_pVisualCh1, SLOT(slotNewTrack()));
    if (view->m_pVisualCh2)
        connect(m_pTrack, SIGNAL(newTrackPlayer2(TrackInfoObject *)), view->m_pVisualCh2, SLOT(slotNewTrack()));



    // Setup state of End of track controls from config database
    ControlObject::getControl(ConfigKey("[Channel1]","TrackEndMode"))->queueFromThread(config->getValueString(ConfigKey("[Controls]","TrackEndModeCh1")).toDouble());
    ControlObject::getControl(ConfigKey("[Channel2]","TrackEndMode"))->queueFromThread(config->getValueString(ConfigKey("[Controls]","TrackEndModeCh2")).toDouble());

    // Initialize preference dialog
    prefDlg = new DlgPreferences(this, view, soundmanager, m_pTrack, config);
    prefDlg->setHidden(true);

#ifdef __LADSPA__
    ladspaDlg = new DlgLADSPA(this);
    ladspaDlg->setHidden(false);
#endif

    // Try open player device If that fails, the preference panel is opened.
    //if (!player->open())
    //    prefDlg->setHidden(false);

    if (soundmanager->setupDevices() != 0)
    {

#ifdef __C_METRICS__
	    cm_writemsg_ascii(MIXXXCMETRICS_FAILED_TO_OPEN_SNDDEVICE_AT_STARTUP,
	                      "Mixxx failed to open audio device(s) on startup.");
#endif
        //TODO: Add cmetric hook for failing to detect audio device on FIRST startup too.
        
        int ret = QMessageBox::warning(this, tr("Mixxx"),
                                        tr("Failed to open your audio device(s).\n"
                                          "Please verify your selection in the preferences."),
                                        QMessageBox::Ok,
                                        QMessageBox::Ok);
         prefDlg->show();
    }

    //setFocusPolicy(QWidget::StrongFocus);
    //grabKeyboard();

    // Load tracks in args.qlMusicFiles (command line arguments) into player 1 and 2:
#ifndef QT3_SUPPORT
    if (args.qlMusicFiles.count()>1)
        m_pTrack->slotLoadPlayer1((*args.qlMusicFiles.at(1)));
    if (args.qlMusicFiles.count()>2)
        m_pTrack->slotLoadPlayer2((*args.qlMusicFiles.at(2)));
#else
    if (args.qlMusicFiles.count()>1)
        m_pTrack->slotLoadPlayer1(args.qlMusicFiles.at(1));
    if (args.qlMusicFiles.count()>2)
        m_pTrack->slotLoadPlayer2(args.qlMusicFiles.at(2));
#endif

    // Set up socket interface
//#ifndef __WIN__
//    new MixxxSocketServer(m_pTrack);
//#endif

    // Initialize visualization of temporal effects
    channel1->setVisual(buffer1);
    channel2->setVisual(buffer2);

    // Dynamic scaling of temporal effect curves
    if (view->m_bZoom)
    {
        ControlObject::connectControls(ConfigKey("[Channel1]", "rate"), ConfigKey("[Channel1]", "VisualLengthScale-temporal"));
        ControlObject::connectControls(ConfigKey("[Channel2]", "rate"), ConfigKey("[Channel2]", "VisualLengthScale-temporal"));
    }

#ifdef __SCRIPT__
    scriptEng = new ScriptEngine(this, m_pTrack);
#endif

    initActions();
    initMenuBar();

    // Check direct rendering
    if (bVisualsWaveform)
        view->checkDirectRendering();

    // Initialize the log if a log file name was given on the command line
    Log *pLog = 0;
    if (args.qLogFileName.length()>0)
        pLog = new Log(args.qLogFileName, m_pTrack);

    //If we were told to start in fullscreen mode on the command-line, then turn on fullscreen mode.
    if (args.bStartInFullscreen)
        slotOptionsFullScreen(true);
#ifdef __C_METRICS__
	cm_writemsg_ascii(MIXXXCMETRICS_MIXXX_CONSTRUCTOR_COMPLETE, "Mixxx constructor complete.");
#endif
}

MixxxApp::~MixxxApp()
{
    QTime qTime;
    qTime.start();

    qDebug("Destroying MixxxApp");

// Moved this up to insulate macros you've worked hard on from being lost in
// a segfault that happens sometimes somewhere below here
#ifdef __SCRIPT__
    scriptEng->saveMacros();
    delete scriptEng;
#endif

    qDebug("Write track xml, %i",qTime.elapsed());
    m_pTrack->writeXML(config->getValueString(ConfigKey("[Playlist]","Listfile")));

    //qDebug("close player, %i",qTime.elapsed());
    //player->close();
    //qDebug("player->close() done");

    qDebug() << "close soundmanager" << qTime.elapsed();
    soundmanager->closeDevices();
    qDebug() << "soundmanager->close() done";

    // Save state of End of track controls in config database
    config->set(ConfigKey("[Controls]","TrackEndModeCh1"), ConfigValue((int)ControlObject::getControl(ConfigKey("[Channel1]","TrackEndMode"))->get()));
    config->set(ConfigKey("[Controls]","TrackEndModeCh2"), ConfigValue((int)ControlObject::getControl(ConfigKey("[Channel2]","TrackEndMode"))->get()));

    //qDebug("delete player, %i",qTime.elapsed());
    //delete player;
    qDebug("delete soundmanager, %i",qTime.elapsed());
    delete soundmanager;
    qDebug("delete master, %i",qTime.elapsed());
    delete master;
    qDebug("delete channel1, %i",qTime.elapsed());
    delete channel1;
    qDebug("delete channel2, %i",qTime.elapsed());
    delete channel2;

    qDebug("delete buffer1, %i",qTime.elapsed());
    delete buffer1;
    qDebug("delete buffer2, %i",qTime.elapsed());
    delete buffer2;

//    qDebug("delete prefDlg");
//    delete m_pControlEngine;
//    qDebug("delete midi");
//    qDebug("delete midiconfig");

    qDebug("delete view, %i",qTime.elapsed());
    delete view;

    qDebug("delete tracks, %i",qTime.elapsed());
    delete m_pTrack;

    delete prefDlg;

    //   delete m_pBpmDetector;
    //   delete m_pWaveSummary;

    qDebug("save config, %i",qTime.elapsed());
    config->Save();
    qDebug("delete config, %i",qTime.elapsed());
    delete config;

    delete frame;
#ifdef __C_METRICS__
	cm_writemsg_ascii(MIXXXCMETRICS_MIXXX_DESTRUCTOR_COMPLETE, "Mixxx deconstructor complete.");
	cm_close(10);
#endif
#ifdef __WIN__
    _exit(0);
#endif
}

/** initializes all QActions of the application */
void MixxxApp::initActions()
{
    fileLoadSongPlayer1 = new QAction(tr("&Load Song (Player 1)..."), this);
    fileLoadSongPlayer1->setShortcut(tr("Ctrl+O"));
    fileLoadSongPlayer1->setShortcutContext(Qt::ApplicationShortcut);

    fileLoadSongPlayer2 = new QAction(tr("&Load Song (Player 2)..."), this);
    fileLoadSongPlayer2->setShortcut(tr("Ctrl+Shift+O"));
    fileLoadSongPlayer2->setShortcutContext(Qt::ApplicationShortcut);

    fileQuit = new QAction(tr("E&xit"), this);
    fileQuit->setShortcut(tr("Ctrl+Q"));
    fileQuit->setShortcutContext(Qt::ApplicationShortcut);

    playlistsNew = new QAction(tr("Add &new playlist"), this);
    playlistsNew->setShortcut(tr("Ctrl+N"));
    playlistsNew->setShortcutContext(Qt::ApplicationShortcut);

    playlistsImport = new QAction(tr("&Import playlist"), this);
    playlistsImport->setShortcut(tr("Ctrl+I"));
    playlistsImport->setShortcutContext(Qt::ApplicationShortcut);

    optionsBeatMark = new QAction(tr("&Audio Beat Marks"), this);

    optionsFullScreen = new QAction(tr("&Full Screen"), this);
    optionsFullScreen->setShortcut(tr("Esc"));
    optionsFullScreen->setShortcutContext(Qt::ApplicationShortcut);
    //QShortcut * shortcut = new QShortcut(QKeySequence(tr("Esc")),  this);
    //    connect(shortcut, SIGNAL(activated()), this, SLOT(slotQuitFullScreen()));

    optionsPreferences = new QAction(tr("&Preferences"), this);
    optionsPreferences->setShortcut(tr("Ctrl+P"));
    optionsPreferences->setShortcutContext(Qt::ApplicationShortcut);

    helpAboutApp = new QAction(tr("&About..."), this);
    helpSupport = new QAction(tr("&Community Support..."), this);
#ifdef __VINYLCONTROL__
    optionsVinylControl = new QAction(tr("&Enable Vinyl Control"), this);
    optionsVinylControl->setShortcut(tr("Ctrl+Y"));
    optionsVinylControl->setShortcutContext(Qt::ApplicationShortcut);
#endif

#ifdef __SCRIPT__
    macroStudio = new QAction(tr("Show Studio"), this);
#endif

    fileLoadSongPlayer1->setStatusTip(tr("Opens a song in player 1"));
    fileLoadSongPlayer1->setWhatsThis(tr("Open\n\nOpens a song in player 1"));
    connect(fileLoadSongPlayer1, SIGNAL(activated()), this, SLOT(slotFileLoadSongPlayer1()));

    fileLoadSongPlayer2->setStatusTip(tr("Opens a song in player 2"));
    fileLoadSongPlayer2->setWhatsThis(tr("Open\n\nOpens a song in player 2"));
    connect(fileLoadSongPlayer2, SIGNAL(activated()), this, SLOT(slotFileLoadSongPlayer2()));

    fileQuit->setStatusTip(tr("Quits the application"));
    fileQuit->setWhatsThis(tr("Exit\n\nQuits the application"));
    connect(fileQuit, SIGNAL(activated()), this, SLOT(slotFileQuit()));

    playlistsNew->setStatusTip(tr("Add new playlist"));
    playlistsNew->setWhatsThis(tr("Add a new playlist"));
    connect(playlistsNew, SIGNAL(activated()), m_pTrack, SLOT(slotNewPlaylist()));

    playlistsImport->setStatusTip(tr("Import playlist"));
    playlistsImport->setWhatsThis(tr("Import playlist"));
    connect(playlistsImport, SIGNAL(activated()), m_pTrack, SLOT(slotImportPlaylist()));

    optionsBeatMark->setCheckable(false);
    optionsBeatMark->setChecked(false);
    optionsBeatMark->setStatusTip(tr("Audio Beat Marks"));
    optionsBeatMark->setWhatsThis(tr("Audio Beat Marks\nMark beats by audio clicks"));
    connect(optionsBeatMark, SIGNAL(toggled(bool)), this, SLOT(slotOptionsBeatMark(bool)));

#ifdef __VINYLCONTROL__
    //Either check or uncheck the vinyl control menu item depending on what it was saved as.
    optionsVinylControl->setCheckable(true);
    if ((bool)config->getValueString(ConfigKey("[VinylControl]","Enabled")).toInt() == true)
        optionsVinylControl->setChecked(true);
    else
        optionsVinylControl->setChecked(false);
    optionsVinylControl->setStatusTip(tr("Activate Vinyl Control"));
    optionsVinylControl->setWhatsThis(tr("Use timecoded vinyls on external turntables to control Mixxx"));
    connect(optionsVinylControl, SIGNAL(toggled(bool)), this, SLOT(slotOptionsVinylControl(bool)));
#endif

    optionsFullScreen->setCheckable(true);
    optionsFullScreen->setChecked(false);
    optionsFullScreen->setStatusTip(tr("Full Screen"));
    optionsFullScreen->setWhatsThis(tr("Display Mixxx using the full screen"));
    connect(optionsFullScreen, SIGNAL(toggled(bool)), this, SLOT(slotOptionsFullScreen(bool)));

    optionsPreferences->setStatusTip(tr("Preferences"));
    optionsPreferences->setWhatsThis(tr("Preferences\nPlayback and MIDI preferences"));
    connect(optionsPreferences, SIGNAL(activated()), this, SLOT(slotOptionsPreferences()));

    helpSupport->setStatusTip(tr("Support..."));
    helpSupport->setWhatsThis(tr("Support\n\nGet help with Mixxx"));
    connect(helpSupport, SIGNAL(activated()), this, SLOT(slotHelpSupport()));

    helpAboutApp->setStatusTip(tr("About the application"));
    helpAboutApp->setWhatsThis(tr("About\n\nAbout the application"));
    connect(helpAboutApp, SIGNAL(activated()), this, SLOT(slotHelpAbout()));

#ifdef __SCRIPT__
    macroStudio->setStatusTip(tr("Shows the macro studio window"));
    macroStudio->setWhatsThis(tr("Show Studio\n\nMakes the macro studio visible"));
    connect(macroStudio, SIGNAL(activated()), scriptEng->getStudio(), SLOT(showStudio()));
#endif
}

void MixxxApp::initMenuBar()
{
    // MENUBAR
    fileMenu=new QMenu("&File");
    optionsMenu=new QMenu("&Options");
    playlistsMenu=new QMenu("&Playlists");
    viewMenu=new QMenu("&View");
    helpMenu=new QMenu("&Help");
#ifdef __SCRIPT__
    macroMenu=new QMenu("&Macro");
#endif

    // menuBar entry fileMenu
    fileMenu->addAction(fileLoadSongPlayer1);
    fileMenu->addAction(fileLoadSongPlayer2);
    fileMenu->addAction(fileQuit);

    // menuBar entry optionsMenu
    //optionsMenu->setCheckable(true);
    //  optionsBeatMark->addTo(optionsMenu);
#ifdef __VINYLCONTROL__
    optionsMenu->addAction(optionsVinylControl);
#endif
    optionsMenu->addAction(optionsFullScreen);
    optionsMenu->addAction(optionsPreferences);

    //    playlistsMenu->setCheckable(true);
    playlistsMenu->addAction(playlistsNew);
    playlistsMenu->addAction(playlistsImport);
    playlistsMenu->addSeparator();
    // menuBar entry viewMenu
    //viewMenu->setCheckable(true);

    // menuBar entry helpMenu
    helpMenu->addAction(helpSupport);
    helpMenu->addSeparator();
    helpMenu->addAction(helpAboutApp);
    

#ifdef __SCRIPT__
    macroMenu->addAction(macroStudio);
#endif

    menuBar()->addMenu(fileMenu);
    menuBar()->addMenu(playlistsMenu);
    menuBar()->addMenu(optionsMenu);

    //    menuBar()->addMenu(viewMenu);
#ifdef __SCRIPT__
    menuBar()->addMenu(macroMenu);
#endif
    menuBar()->addSeparator();
    menuBar()->addMenu(helpMenu);
    new MixxxMenuPlaylists(playlistsMenu, m_pTrack);

}

bool MixxxApp::queryExit()
{
    int exit=QMessageBox::information(this, tr("Quit..."),
                                      tr("Do your really want to quit?"),
                                      QMessageBox::Ok, QMessageBox::Cancel);

    if (exit==1)
    {
    }
    else
    {
    };

    return (exit==1);
}

void MixxxApp::slotFileLoadSongPlayer1()
{
    ControlObject* play = ControlObject::getControl(ConfigKey("[Channel1]", "play"));
    
    if (play->get() == 1.)
    {
        int ret = QMessageBox::warning(this, tr("Mixxx"),
                                        tr("Player 1 is currently playing a song.\n"
                                          "Are you sure you want to load a new song?"),
                                        QMessageBox::Yes | QMessageBox::No,
                                        QMessageBox::No);
                                        
        if (ret != QMessageBox::Yes)
            return;
    }

    QString s = QFileDialog::getOpenFileName(this, "Load Song into Player 1", config->getValueString(ConfigKey("[Playlist]","Directory")),
                                             SUPPORTED_AUDIO_FILES);
    if (!(s == QString::null)) {
        TrackInfoObject * pTrack = m_pTrack->getTrackCollection()->getTrack(s);
        if (pTrack)
            m_pTrack->slotLoadPlayer1(pTrack);
    }
}

void MixxxApp::slotFileLoadSongPlayer2()
{
    ControlObject* play = ControlObject::getControl(ConfigKey("[Channel2]", "play"));
    
    if (play->get() == 1.)
    {
        int ret = QMessageBox::warning(this, tr("Mixxx"),
                                        tr("Player 2 is currently playing a song.\n"
                                          "Are you sure you want to load a new song?"),
                                        QMessageBox::Yes | QMessageBox::No,
                                        QMessageBox::No);
                                        
        if (ret != QMessageBox::Yes)
            return;
    }
    
    QString s = QFileDialog::getOpenFileName(this, "Load Song into Player 2", config->getValueString(ConfigKey("[Playlist]","Directory")),
                                             SUPPORTED_AUDIO_FILES);
    if (!(s == QString::null)) {
        TrackInfoObject * pTrack = m_pTrack->getTrackCollection()->getTrack(s);
        if (pTrack)
            m_pTrack->slotLoadPlayer2(pTrack);
    }
}

void MixxxApp::slotFileQuit()
{
    qApp->quit();
}

void MixxxApp::slotOptionsBeatMark(bool)
{
// BEAT MARK STUFF
}

// void MixxxApp::slotQuitFullScreen() {
//     if (optionsFullScreen->isChecked())
//         slotOptionsFullScreen(false);
// }

void MixxxApp::slotOptionsFullScreen(bool toggle)
{
    // Making a fullscreen window on linux and windows is harder than you could possibly imagine...
    if (toggle)
    {
#ifdef __LINUX__
         winpos = pos();
         // Can't set max to -1,-1 or 0,0 for unbounded?
         setMaximumSize(32767,32767);
#endif

        showFullScreen();
        //menuBar()->hide();
        // FWI: Begin of fullscreen patch
#ifdef __LINUX__
        // Crazy X window managers break this so I'm told by Qt docs
        //         int deskw = app->desktop()->width();
        //         int deskh = app->desktop()->height();

        //support for xinerama
        int deskw = app->desktop()->screenGeometry(frame).width();
        int deskh = app->desktop()->screenGeometry(frame).height();
#else
        int deskw = width();
        int deskh = height();
#endif
        view->move((deskw - view->width())/2, (deskh - view->height())/2);
        // FWI: End of fullscreen patch
    }
    else
    {
        // FWI: Begin of fullscreen patch
        view->move(0,0);
        menuBar()->show();
        showNormal();

#ifdef __LINUX__
        if (size().width() != view->width() ||
            size().height() != view->height() + menuBar()->height()) {
          setFixedSize(view->width(), view->height() + menuBar()->height());
        }
        move(winpos);
#endif

        // FWI: End of fullscreen patch
    }
}

void MixxxApp::slotOptionsPreferences()
{
    prefDlg->setHidden(false);
}

//Note: Can't #ifdef this because MOC doesn't catch it.
void MixxxApp::slotOptionsVinylControl(bool toggle)
{
    //qDebug("slotOptionsVinylControl: toggle is %i", (int)toggle);

    QString device1 = config->getValueString(ConfigKey("[VinylControl]","DeviceInputDeck1")); 
    QString device2 = config->getValueString(ConfigKey("[VinylControl]","DeviceInputDeck2"));

    if (device1 == "" && device2 == "" && (toggle==true))
    {
        int ret = QMessageBox::warning(this, tr("Mixxx"),
                                    tr("No input device(s) select.\n"
                                      "Please select your soundcard(s) in vinyl control preferences."),
                                    QMessageBox::Ok,
                                    QMessageBox::Ok);
        prefDlg->show();
        prefDlg->showVinylControlPage();
    }
    else
    {
        config->set(ConfigKey("[VinylControl]","Enabled"), ConfigValue((int)toggle));
        ControlObject::getControl(ConfigKey("[VinylControl]", "Enabled"))->set((int)toggle);
    }
}

void MixxxApp::slotHelpAbout()
{

    DlgAbout *about = new DlgAbout(this);
    about->version_label->setText(VERSION);
    QString credits = 
    "<p align=\"center\"><b>Mixxx 1.6.0 Development Team</b></p>"
"<p align=\"center\">"
"Adam Davison<br>"
"Albert Santoni<br>"
"Cedric Gestes<br>"
"Garth Dahlstrom<br>"
"John Sully<br>"
"Ben Wheeler<br>"
"Micah Lee<br>"
"Pawel Bartkiewicz<br>"
"Nathan Prado<br>"
"</p>"
"<p align=\"center\"><b>With contributions from:</b></p>"
"<p align=\"center\">"
"Mark Hills<br>"
"Martin Sakm&#225;r<br>"
"Alex Barker<br>"
"Dave Jarvis<br>"
"Thomas Baag<br>"
"Karlis Kalnins<br>"
"Amias Channer<br>"
"Sacha Berger<br>"
"Stefan Langhammer<br>"
"Andre Roth<br>"
"Frank Willascheck<br>"
"Robin Sheat<br>"
"Jeff Nelson<br>"
"Wesley Stessens<br>"
"Kevin Schaper<br>"
"</p>"
"<p align=\"center\"><b>And special thanks to:</b></p>"
"<p align=\"center\">"
"Adam Bellinson<br>"
"Melanie Thielker<br>"
"Julien Rosener<br>"
"Pau Arumí<br>"
"David Garcia<br>"
"Seb Ruiz<br>"
"</p>"

"<p align=\"center\"><b>Past Contributors</b></p>"
"<p align=\"center\">"
"Tue Haste Andersen<br>"
"Ken Haste Andersen<br>"
"Ludek Hor&#225;cek<br>"
"Svein Magne Bang<br>"
"Kristoffer Jensen<br>"
"Ingo Kossyk<br>"
"Torben Hohn<br>"
"Peter Chang<br>"
"Mads Holm<br>"
"Lukas Zapletal<br>"
"Jeremie Zimmermann<br>"
"Gianluca Romanin<br>"
"Tim Jackson<br>"
"Jan Jockusch<br>"
"</p>";


    about->textBrowser->setHtml(credits);
    about->show();

}

void MixxxApp::slotHelpSupport()
{
    QUrl qSupportURL;
    qSupportURL.setUrl(MIXXX_SUPPORT_URL);
    QDesktopServices::openUrl(qSupportURL);
}

void MixxxApp::rebootMixxxView() {

    // Ok, so wierdly if you call setFixedSize with the same value twice, Qt breaks
    // So we check and if the size hasn't changed we don't make the call
    int oldh = view->height();
    int oldw = view->width();
    qDebug("Now in Rebootmixxview...");
    bool bVisualsWaveform = true;
    //TODO 0 or 1?
    if (config->getValueString(ConfigKey("[Controls]","Visuals")).toInt()==1)
        bVisualsWaveform = false;

    QString qSkinPath = getSkinPath();

    view->rebootGUI(frame, bVisualsWaveform, config, qSkinPath);
    qDebug("rebootgui DONE");
    if (oldw != view->width() || oldh != view->height() + menuBar()->height()) {
      setFixedSize(view->width(), view->height() + menuBar()->height());
    }
}

QString MixxxApp::getSkinPath() {
    QString qConfigPath = config->getConfigPath();

    QString qSkinPath(qConfigPath);
    qSkinPath.append("skins/");
    if (QDir(qSkinPath).exists())
    {
        // Is the skin listed in the config database there? If not, use default (outlineSmall) skin
        if ((config->getValueString(ConfigKey("[Config]","Skin")).length()>0 && QDir(QString(qSkinPath).append(config->getValueString(ConfigKey("[Config]","Skin")))).exists()))
            qSkinPath.append(config->getValueString(ConfigKey("[Config]","Skin")));
        else
        {
            config->set(ConfigKey("[Config]","Skin"), ConfigValue("outlineSmall"));
            config->Save();
            qSkinPath.append(config->getValueString(ConfigKey("[Config]","Skin")));
        }
    }
    else
        qCritical() << "Skin directory does not exist:" << qSkinPath;

    return qSkinPath;
}
