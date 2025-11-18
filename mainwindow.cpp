#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "wrapper.cc"

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/foreach.hpp>
#include <boost/thread/thread.hpp>
#include <boost/chrono.hpp>
#include <iostream>

#include <fstream>
#include <bitset>

using namespace eemagine::sdk;

//---------------------------------------------------------------------------
// Layouts
//---------------------------------------------------------------------------

// 32 channels CA-209 cap
static const std::vector<std::string> electrodeMap_209 =
{
    "FP1",   "FPz",    "FP2",   "F7",     "F3",    "Fz",     "F4",     "F8",
    "FC5",   "FC1",    "FC2",   "FC6",    "M1",    "T7",     "C3",     "Cz",
    "C4",    "T8",     "M2",    "CP5",    "CP1",   "CP2",    "CP6",    "P7",
    "P3",    "Pz",     "P4",    "P8",     "POz",   "O1",     "Oz",     "O2"};

// 32 channels CA-001 cap
static const std::vector<std::string> electrodeMap_001 = {
    "AF3",   "AF4",    "F3",   "F1",     "Fz",    "F2",     "F4",     "FC3",
    "FC1",   "FCz",    "FC2",  "FC4",    "C3",    "C1",     "Cz",    "C2",  
    "C4",    "CP3",    "CP1",   "CPz",  "CP2",    "CP4",   "P3",     "P1", 
    "Pz",    "P2",     "P4",    "PO3",  "POz",    "PO4",   "O1",     "O2"};

// 64 channels CA-200 cap
static const std::vector<std::string> electrodeMap_200 = {
    "FP1",   "FPz",    "FP2",   "F7",     "F3",    "Fz",     "F4",     "F8",
    "FC5",   "FC1",    "FC2",   "FC6",    "M1",    "T7",     "C3",     "Cz",
    "C4",    "T8",     "M2",    "CP5",    "CP1",   "CP2",    "CP6",    "P7",
    "P3",    "Pz",     "P4",    "P8",     "POz",   "O1",     "O2",     "EOG",
    "AF7",   "AF3",    "AF4",   "AF8",    "F5",    "F1",     "F2",     "F6",
    "FC3",   "FCz",    "FC4",   "C5",     "C1",    "C2",     "C6",     "CP3",
    "CP4",   "P5",     "P1",    "P2",     "P6",    "PO5",    "PO3",    "PO4",
    "PO6",   "FT7",    "FT8",   "TP7",    "TP8",   "PO7",    "PO8",    "Oz"};

static const std::vector<std::string> electrodeMap_bip = {
    "AUX1",     "AUX2",     "AUX3",     "AUX4",     "AUX5",     "AUX6",     "AUX7",
    "AUX8",     "AUX9",     "AUX10",    "AUX11",    "AUX12",    "AUX13",    "AUX14",
    "AUX15",    "AUX16",    "AUX17",    "AUX18",    "AUX19",    "AUX20",    "AUX21",
    "AUX22",    "AUX23",    "AUX24"
};


MainWindow::MainWindow(QWidget *parent, const std::string &config_file, const bool linkOnStart) : QMainWindow(parent)
{
    reader = nullptr;
    ui.setupUi(this);

    // Init initial indexes
    if(!config_file.empty())
        load_config(config_file);
    else
        ui.Cap_ID->setCurrentIndex(1);
        ui.samplingRate->setCurrentIndex(1);
        ui.EEG_Range->setCurrentIndex(0);
        ui.BIP_Range->setCurrentIndex(2);


    // make GUI connections
    QObject::connect(ui.actionLoad_Configuration, SIGNAL(triggered()), this, SLOT(load_config_dialog()));
    QObject::connect(ui.actionSave_Configuration, SIGNAL(triggered()), this, SLOT(save_config_dialog()));
    QObject::connect(ui.actionQuit, SIGNAL(triggered()), this, SLOT(close()));
    QObject::connect(ui.linkButton, SIGNAL(clicked()), this, SLOT(link()));
    if(linkOnStart) {
        link();
    }
}

void MainWindow::load_config_dialog() {
    QString sel = QFileDialog::getOpenFileName(this, "Load Configuration File", "", "Configuration Files (*.cfg)");
    if (!sel.isEmpty())
        load_config(sel.toStdString());
}

void MainWindow::save_config_dialog() {
    QString sel = QFileDialog::getSaveFileName(this, "Save Configuration File", "", "Configuration Files (*.cfg)");
    if (!sel.isEmpty())
        save_config(sel.toStdString());
}

void MainWindow::closeEvent(QCloseEvent *ev) {
    if (reader != nullptr) {
        ev->ignore();
    }
}

void MainWindow::load_config(const std::string &filename) {
    using boost::property_tree::ptree;
    ptree pt;

    // parse file
    try {
        read_xml(filename, pt);
    }
    catch (std::exception &e) {
        QMessageBox::information(this, "Error", (std::string("Cannot read config file: ") += e.what()).c_str(), QMessageBox::Ok);
        return;
    }

    // get config values
    try {
        ui.Cap_ID->setCurrentIndex(pt.get<int>("settings.capId", 1));
        ui.samplingRate->setCurrentIndex(pt.get<int>("settings.samplingrate", 2));
        ui.EEG_Range->setCurrentIndex(pt.get<int>("settings.EegRange", 0));
        ui.BIP_Range->setCurrentIndex(pt.get<int>("settings.BipRange", 2));

        std::string tmp = pt.get<std::string>("settings.hexEegMask", "0xFFFFFFFFFFFFFFFF");
        ui.EEG_LineEdit->setText(QString::fromStdString(tmp));
        tmp = pt.get<std::string>("settings.hexBipMask", "0x000000");
        ui.BIP_LineEdit->setText(QString::fromStdString(tmp));
    }
    catch (std::exception &) {
        QMessageBox::information(this, "Error in Config File", "Could not read out config parameters.", QMessageBox::Ok);
        return;
    }
}

void MainWindow::save_config(const std::string &filename) {
    using boost::property_tree::ptree;
    ptree pt;

    // transfer UI content into property tree
    try {
        pt.put("settings.capId", ui.Cap_ID->currentIndex());
        pt.put("settings.samplingrate", ui.samplingRate->currentIndex());
        pt.put("settings.EegRange", ui.EEG_Range->currentIndex());
        pt.put("settings.BipRange", ui.BIP_Range->currentIndex());

        QString tmp = ui.EEG_LineEdit->text();
        pt.put("settings.hexEegMask", tmp.toStdString());
        tmp = ui.BIP_LineEdit->text();
        pt.put("settings.hexBipMask", tmp.toStdString());

    }
    catch (std::exception &e) {
        QMessageBox::critical(this, "Error", (std::string("Could not prepare settings for saving: ") += e.what()).c_str(), QMessageBox::Ok);
    }

    // write to disk
    try {
        write_xml(filename, pt);
    }
    catch (std::exception &e) {
        QMessageBox::critical(this, "Error", (std::string("Could not write to config file: ") += e.what()).c_str(), QMessageBox::Ok);
    }
}

void MainWindow::link() {
    if (reader != nullptr) {
        reader->setStop(true);
        ui.linkButton->setText("Link");
    }
    else {
        // === perform link action ===
        try {
            // get the UI parameters...
            QString tmpParam;

            // CapId
            int capId = ui.Cap_ID->currentIndex();

            // Samping rate
            tmpParam = ui.samplingRate->currentText();
            int samplingRate = tmpParam.toInt();

            // Eeg channels Range
            tmpParam = ui.EEG_Range->currentText();
            double EegRange = tmpParam.toDouble()/ 1000;

            // BIP channels range
            tmpParam = ui.BIP_Range->currentText();
            double BipRange = tmpParam.toDouble() / 1000;

            // EEG channels mask
            tmpParam = ui.EEG_LineEdit->text();
            std::string tmpString = tmpParam.toStdString();
            unsigned long long hexEegMask = std::strtoull(tmpString.c_str(), nullptr, 16);

            // BIP channels mask
            tmpParam = ui.BIP_LineEdit->text();
            tmpString = tmpParam.toStdString();
            unsigned long long hexBipMask = std::strtoull(tmpString.c_str(), nullptr, 16);

            thread = new QThread();
            reader = new Reader();

            reader->setParams(capId, samplingRate, BipRange, EegRange, hexEegMask, hexBipMask);

            reader->moveToThread(thread);
            connect(thread, SIGNAL(started()), reader, SLOT(read()));

            connect(reader, SIGNAL(finished()), thread, SLOT(quit()));
            connect(thread, SIGNAL(finished()), this, SLOT(threadFinished()));
            connect(reader, SIGNAL(finished()), reader, SLOT(deleteLater()));
            connect(thread, SIGNAL(finished()), thread, SLOT(deleteLater()));
            connect(reader, SIGNAL(timeout()), this, SLOT(threadTimeout()));
            connect(reader, SIGNAL(connectionLost()), this, SLOT(connectionLost()));
            connect(reader, SIGNAL(ampNotFound()), this, SLOT(ampNotFound()));
            connect(reader, SIGNAL(unknownError()), this, SLOT(unknownError()));
            thread->start();
        }
        catch (std::exception &e) {
            // try to decode the error message
            std::string msg = "Could not query driver message because the device is not open";
            QMessageBox::critical(this, "Error", ("Could not initialize the eego Sport interface: " + (e.what() + (" (driver message: " + msg + ")"))).c_str(), QMessageBox::Ok);
            return;
        }

        // done, all successful
        ui.linkButton->setText("Unlink");
    }
}

void MainWindow::threadFinished() {
    reader = nullptr;
    delete thread;
    thread = nullptr;
    ui.linkButton->setText("Link");
}

void MainWindow::threadTimeout() {
    QMessageBox::critical(this, "Error", (std::string("Error: eego Sport Conncetion timed out")).c_str(), QMessageBox::Ok);
}

void MainWindow::ampNotFound() {
    QMessageBox::critical(this, "Error", (std::string("Error: Amp not found or license file not present.\nPlease connect the amplifier and make sure that the amp is turned on.")).c_str(), QMessageBox::Ok);
}

void MainWindow::connectionLost() {
    QMessageBox::critical(this, "Error", (std::string("Error: Amp connection lost")).c_str(), QMessageBox::Ok);
}

void MainWindow::unknownError() {
    QMessageBox::critical(this, "Error", (std::string("Error: Unknown")).c_str(), QMessageBox::Ok);
}

// --- CONSTRUCTOR ---
Reader::Reader() {
    stop = false;
    // you could copy data from constructor arguments to internal variables here.
}

// --- DECONSTRUCTOR ---
Reader::~Reader() {
    // free resources
}

void Reader::setParams(int capId, int samplingRate, double BipRange, double EegRange, unsigned long long hexEegMask, unsigned long long hexBipMask) {

    // Define the proper channels layout
    if (capId == 0){
        this->capLayout = electrodeMap_209;
    } else if (capId == 1){
        this->capLayout = electrodeMap_001;
    } else if (capId == 2){
        this->capLayout = electrodeMap_200;
    }
    // other amplifiers parameters
    this->samplingRate = samplingRate;
    this->BipRange = BipRange;
    this->EegRange = EegRange;
    this->hexEegMask = hexEegMask;
    this->hexBipMask = hexBipMask;

}

// Sorting the amplifiers according to their serial number. First amplifier to connect should be the one with the lowest SN.
template <typename T>
std::vector<size_t> sort_indexes(const std::vector<T> &v) {

    // initialize original index locations
    std::vector<size_t> idx(v.size());
    iota(idx.begin(), idx.end(), 0);

    // sort indexes based on comparing values in v
    sort(idx.begin(), idx.end(),
         [&v](size_t i1, size_t i2) {return v[i1] < v[i2];});

    return idx;
}

void Reader::OpenEegStream() {
    std::vector<eemagine::sdk::amplifier*> amps;
    std::vector<std::string> serialTmp;
    amps = fact.getAmplifiers();

    // Check if several amplifiers connected (128Ch)
    if ( amps.size() > 1 ) {

        for (unsigned int i = 0; i < amps.size(); ++i)
            serialTmp.push_back(amps.at(i)->getSerialNumber());
        std::vector<eemagine::sdk::amplifier*>  tmpAmps;
        for (auto i : sort_indexes(serialTmp)) {
            tmpAmps.push_back(amps.at(i));
        }
        amp = fact.createCascadedAmplifier(tmpAmps);
        // or only one
    } else if(amps.size() == 1 )
        amp = amps.at(0);
    else
        throw(exceptions::notFound("No amp found"));
    // EEG stream's instanciation
    if ( amp ) {
        eegStream = amp->OpenEegStream(samplingRate, EegRange, BipRange, hexEegMask, hexBipMask);
    }
}

// --- PROCESS ---
// Start processing data.
void Reader::read() {
    bool ampFound = true;
    try {

        OpenEegStream();
        std::vector<channel> channelList = eegStream->getChannelList();
        // create data streaminfo and append some meta-data
        lsl::stream_info data_info("eegoSports " + amp->getSerialNumber(), "EEG", static_cast<int32_t>(channelList.size()) - 1, samplingRate, lsl::cf_double64, "eegoSports_" + amp->getSerialNumber());
        lsl::xml_element channels = data_info.desc().append_child("channels");

        // convert the eeg mask to a bits array for ch labelling
        std::bitset<64> hexEegMask_bits(this->hexEegMask);
        for (unsigned int k = 0; k < static_cast<unsigned int>(hexEegMask_bits.size()); k++) {
            if (hexEegMask_bits[k] == 1) {
                std::string ch_name = this->capLayout.at(k);
                channels.append_child("channel")
                        .append_child_value("label", ch_name)
                        .append_child_value("type", "EEG")
                        .append_child_value("unit", "uV");
            }
        }
        // convert the bip mask to a bits array for ch labelling
        std::bitset<24> hexBipMask_bits(this->hexBipMask);
        for (unsigned int k = 0; k < static_cast<unsigned int>(hexBipMask_bits.size()); k++) {
            if (hexBipMask_bits[k] == 1) {
                std::string ch_name = electrodeMap_bip.at(k);
                channels.append_child("channel")
                        .append_child_value("label", ch_name)
                        .append_child_value("type", "AUX")
                        .append_child_value("unit", "uV");
            }
        }

        channels.append_child("channel")
                .append_child_value("label", "TRIGGER")
                .append_child_value("type", "Markers")
                .append_child_value("unit", "[]");

        data_info.desc().append_child("acquisition")
                .append_child_value("manufacturer", "antneuro")
                .append_child_value("serial_number", boost::lexical_cast<std::string>(amp->getSerialNumber()).c_str());

        // make a data outlet
        lsl::stream_outlet data_outlet(data_info);

        std::vector<channel> eegChannelList = eegStream->getChannelList();


        while (!stop) {

            //Sleep(8);
            boost::this_thread::sleep_for(boost::chrono::milliseconds(8));

            buffer = eegStream->getData();
            unsigned int channelCount = buffer.getChannelCount();
            unsigned int sampleCount = static_cast<unsigned int>(buffer.size() / channelCount);
            std::cout<<channelCount<<std::endl;

            std::vector<std::vector<double>> send_buffer(sampleCount, std::vector<double>(channelCount - 1));

            for (unsigned int s = 0; s < sampleCount; s++) {
                for (unsigned int c = 0; c < channelCount - 1; c++) {
                    // stream data in uV (better than small values in V) except triggers
                    if (c == channelCount - 2)
                        send_buffer[s][c] = buffer.getSample(c, s);
                    else
                        send_buffer[s][c] = buffer.getSample(c, s)*1000000;
                }
            }

            double now = lsl::local_clock();
            data_outlet.push_chunk(send_buffer, now);

        }
    }
    catch (exceptions::notFound) {
        ampFound = false;
        emit ampNotFound();
    }
    catch (exceptions::notConnected) {
        emit connectionLost();
    }
    catch (exceptions::unknown) {
        emit unknownError();
    }
    catch(std::exception &e) {
        std::cout<<e.what()<<std::endl;
    }
    if (ampFound) {
        delete eegStream;
        delete amp;
    }
    emit finished();
}

MainWindow::~MainWindow() {
}
