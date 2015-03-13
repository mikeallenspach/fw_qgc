/*=====================================================================

QGroundControl Open Source Ground Control Station

(c) 2009, 2010 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>

This file is part of the QGROUNDCONTROL project

    QGROUNDCONTROL is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    QGROUNDCONTROL is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with QGROUNDCONTROL. If not, see <http://www.gnu.org/licenses/>.

======================================================================*/

/**
 * @file
 *   @brief Implementation of SerialConfigurationWindow
 *   @author Lorenz Meier <mavteam@student.ethz.ch>
 *
 */

#include <QDir>
#include <QSettings>
#include <QFileInfoList>
#include <QDebug>

#include <SerialConfigurationWindow.h>
#include <SerialLink.h>

#ifndef USE_ANCIENT_RATES
#define USE_ANCIENT_RATES 0
#endif

SerialConfigurationWindow::SerialConfigurationWindow(SerialConfiguration *config, QWidget *parent, Qt::WindowFlags flags)
    : QWidget(parent, flags)
    , _userConfigured(false)
{
    _ui.setupUi(this);
    Q_ASSERT(config != NULL);
    _config = config;

    // Scan for serial ports. Let the user know if none were found for debugging purposes
    if (!setupPortList()) {
        qDebug() << "No serial ports found.";
    }

    // Set up baud rates
    _ui.baudRate->clear();

    // Keep track of all desired baud rates by OS. These are iterated through
    // later and added to _ui.baudRate.
    QList<int> supportedBaudRates;

#if USE_ANCIENT_RATES
    // Baud rates supported only by POSIX systems
#if defined(Q_OS_UNIX) || defined(Q_OS_LINUX) || defined(Q_OS_DARWIN)
    supportedBaudRates << 50;
    supportedBaudRates << 75;
    supportedBaudRates << 134;
    supportedBaudRates << 150;
    supportedBaudRates << 200;
    supportedBaudRates << 1800;
#endif
#endif //USE_ANCIENT_RATES

    // Baud rates supported only by Windows
#if defined(Q_OS_WIN)
    supportedBaudRates << 14400;
    supportedBaudRates << 56000;
    supportedBaudRates << 128000;
    supportedBaudRates << 256000;
#endif

    // Baud rates supported by everyone
#if USE_ANCIENT_RATES
    supportedBaudRates << 110;
    supportedBaudRates << 300;
    supportedBaudRates << 600;
    supportedBaudRates << 1200;
#endif //USE_ANCIENT_RATES
    supportedBaudRates << 2400;
    supportedBaudRates << 4800;
    supportedBaudRates << 9600;
    supportedBaudRates << 19200;
    supportedBaudRates << 38400;
    supportedBaudRates << 57600;
    supportedBaudRates << 115200;
    supportedBaudRates << 230400;
    supportedBaudRates << 460800;

#if defined(Q_OS_LINUX)
    // Baud rates supported only by Linux
    supportedBaudRates << 500000;
    supportedBaudRates << 576000;
#endif

    supportedBaudRates << 921600;

    // Now actually add all of our supported baud rates to the ui.
    qSort(supportedBaudRates.begin(), supportedBaudRates.end());
    for (int i = 0; i < supportedBaudRates.size(); ++i) {
        _ui.baudRate->addItem(QString::number(supportedBaudRates.at(i)), supportedBaudRates.at(i));
    }

    // Connect the individual user interface inputs
    connect(_ui.portName,           SIGNAL(editTextChanged(QString)), this,      SLOT(setPortName(QString)));
    connect(_ui.portName,           SIGNAL(currentIndexChanged(QString)), this,  SLOT(setPortName(QString)));
    connect(_ui.baudRate,           SIGNAL(activated(int)), this,                SLOT(setBaudRate(int)));
    connect(_ui.flowControlCheckBox,SIGNAL(toggled(bool)), this,                 SLOT(enableFlowControl(bool)));
    connect(_ui.parNone,            SIGNAL(toggled(bool)), this,                 SLOT(setParityNone(bool)));
    connect(_ui.parOdd,             SIGNAL(toggled(bool)), this,                 SLOT(setParityOdd(bool)));
    connect(_ui.parEven,            SIGNAL(toggled(bool)), this,                 SLOT(setParityEven(bool)));
    connect(_ui.dataBitsSpinBox,    SIGNAL(valueChanged(int)), this,             SLOT(setDataBits(int)));
    connect(_ui.stopBitsSpinBox,    SIGNAL(valueChanged(int)), this,             SLOT(setStopBits(int)));
    connect(_ui.advCheckBox,        SIGNAL(clicked(bool)), _ui.advGroupBox,      SLOT(setVisible(bool)));

    _ui.advCheckBox->setCheckable(true);
    _ui.advCheckBox->setChecked(false);
    _ui.advGroupBox->setVisible(false);

    switch(_config->parity()) {
    case QSerialPort::NoParity:
        _ui.parNone->setChecked(true);
        break;
    case QSerialPort::OddParity:
        _ui.parOdd->setChecked(true);
        break;
    case QSerialPort::EvenParity:
        _ui.parEven->setChecked(true);
        break;
    default:
        // Enforce default: no parity in link
        setParityNone(true);
        _ui.parNone->setChecked(true);
        break;
    }

    int idx = 0;
    _ui.flowControlCheckBox->setChecked(_config->flowControl() == QSerialPort::HardwareControl);
    idx = _ui.baudRate->findText(QString("%1").arg(_config->baud()));
    if(idx < 0) idx = _ui.baudRate->findText("57600");
    if(idx < 0) idx = 0;
    _ui.baudRate->setCurrentIndex(idx);
    _ui.dataBitsSpinBox->setValue(_config->dataBits());
    _ui.stopBitsSpinBox->setValue(_config->stopBits());
    _portCheckTimer = new QTimer(this);
    _portCheckTimer->setInterval(1000);
    connect(_portCheckTimer, SIGNAL(timeout()), this, SLOT(setupPortList()));

    // Display the widget
    setWindowTitle(tr("Serial Communication Settings"));
}

SerialConfigurationWindow::~SerialConfigurationWindow()
{

}

void SerialConfigurationWindow::showEvent(QShowEvent* event)
{
    Q_UNUSED(event);
    _portCheckTimer->start();
}

void SerialConfigurationWindow::hideEvent(QHideEvent* event)
{
    Q_UNUSED(event);
    _portCheckTimer->stop();
}

bool SerialConfigurationWindow::setupPortList()
{
    // Get the ports available on this system
    QList<QString> ports = SerialConfiguration::getCurrentPorts();
    QString storedName = _config->portName();
    bool storedFound = false;
    // Add the ports in reverse order, because we prepend them to the list
    for (int i = ports.count() - 1; i >= 0; --i)
    {
        // Prepend newly found port to the list
        if (_ui.portName->findText(ports[i]) < 0)
        {
            _ui.portName->insertItem(0, ports[i]);
            if (!_userConfigured) _ui.portName->setEditText(ports[i]);
        }
        // Check if the stored link name is still present
        if (ports[i].contains(storedName) || storedName.contains(ports[i]))
            storedFound = true;
    }
    if (storedFound)
        _ui.portName->setEditText(storedName);
    return (ports.count() > 0);
}

void SerialConfigurationWindow::enableFlowControl(bool flow)
{
    _config->setFlowControl(flow ? QSerialPort::HardwareControl : QSerialPort::NoFlowControl);
}

void SerialConfigurationWindow::setParityNone(bool accept)
{
    if (accept) _config->setParity(QSerialPort::NoParity);
}

void SerialConfigurationWindow::setParityOdd(bool accept)
{
    if (accept) _config->setParity(QSerialPort::OddParity);
}

void SerialConfigurationWindow::setParityEven(bool accept)
{
    if (accept) _config->setParity(QSerialPort::EvenParity);
}

void SerialConfigurationWindow::setPortName(QString port)
{
#ifdef Q_OS_WIN
    port = port.split("-").first();
#endif
    port = port.trimmed();
    if (_config->portName() != port) {
        _config->setPortName(port);
    }
    userConfigured = true;
}

void SerialConfigurationWindow::setBaudRate(int index)
{
    int baud = _ui.baudRate->itemData(index).toInt();
    _config->setBaud(baud);
}

void SerialConfigurationWindow::setDataBits(int bits)
{
    _config->setDataBits(bits);
}

void SerialConfigurationWindow::setStopBits(int bits)
{
    _config->setStopBits(bits);
}
