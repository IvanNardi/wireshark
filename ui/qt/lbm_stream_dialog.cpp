/* lbm_stream_dialog.cpp
 *
 * Copyright (c) 2005-2014 Informatica Corporation. All Rights Reserved.
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// Adapted from stats_tree_packet.cpp

#include "lbm_stream_dialog.h"
#include <ui_lbm_stream_dialog.h>

#include "file.h"

#include <ui/qt/utils/qt_ui_utils.h>
#include "main_application.h"

#include <QClipboard>
#include <QMessageBox>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <epan/packet_info.h>
#include <epan/to_str.h>
#include <epan/tap.h>
#include <epan/dissectors/packet-lbm.h>

#include <QDebug>

namespace
{
    static const int Stream_Column = 0;
    static const int EndpointA_Column = 1;
    static const int EndpointB_Column = 2;
    static const int Messages_Column = 3;
    static const int Bytes_Column = 4;
    static const int FirstFrame_Column = 5;
    static const int LastFrame_Column = 6;
}

class LBMSubstreamEntry
{
    public:
        LBMSubstreamEntry(uint64_t channel, uint32_t substream_id, const address * source_address, uint16_t source_port, const address * destination_address, uint16_t destination_port);
        ~LBMSubstreamEntry(void);
        void processPacket(uint32_t frame, uint32_t bytes);
        void setItem(QTreeWidgetItem * item);
        QTreeWidgetItem * getItem(void)
        {
            return (m_item);
        }

    private:
        void fillItem(bool update_only = true);
        uint64_t m_channel;
        uint32_t m_substream_id;
        QString m_endpoint_a;
        QString m_endpoint_b;
        uint32_t m_first_frame;
        uint32_t m_flast_frame;
        uint32_t m_messages;
        uint32_t m_bytes;
        QTreeWidgetItem * m_item;
};

LBMSubstreamEntry::LBMSubstreamEntry(uint64_t channel, uint32_t substream_id, const address * source_address, uint16_t source_port, const address * destination_address, uint16_t destination_port) :
    m_channel(channel),
    m_substream_id(substream_id),
    m_first_frame((uint32_t)(~0)),
    m_flast_frame(0),
    m_messages(0),
    m_bytes(0),
    m_item(NULL)
{
    m_endpoint_a = QStringLiteral("%1:%2")
        .arg(address_to_qstring(source_address))
        .arg(source_port);
    m_endpoint_b = QStringLiteral("%1:%2")
        .arg(address_to_qstring(destination_address))
        .arg(destination_port);
}

LBMSubstreamEntry::~LBMSubstreamEntry(void)
{
}

void LBMSubstreamEntry::processPacket(uint32_t frame, uint32_t bytes)
{
    if (m_first_frame > frame)
    {
        m_first_frame = frame;
    }
    if (m_flast_frame < frame)
    {
        m_flast_frame = frame;
    }
    m_bytes += bytes;
    m_messages++;
    fillItem();
}

void LBMSubstreamEntry::setItem(QTreeWidgetItem * item)
{
    m_item = item;
    fillItem(false);
}

void LBMSubstreamEntry::fillItem(bool update_only)
{
    if (update_only == false)
    {
        m_item->setText(Stream_Column, QStringLiteral("%1.%2").arg(m_channel).arg(m_substream_id));
        m_item->setText(EndpointA_Column, m_endpoint_a);
        m_item->setText(EndpointB_Column, m_endpoint_b);
    }
    m_item->setText(Messages_Column, QStringLiteral("%1").arg(m_messages));
    m_item->setText(Bytes_Column, QStringLiteral("%1").arg(m_bytes));
    m_item->setText(FirstFrame_Column, QStringLiteral("%1").arg(m_first_frame));
    m_item->setText(LastFrame_Column, QStringLiteral("%1").arg(m_flast_frame));
}

typedef QMap<uint32_t, LBMSubstreamEntry *> LBMSubstreamMap;
typedef QMap<uint32_t, LBMSubstreamEntry *>::iterator LBMSubstreamMapIterator;

class LBMStreamEntry
{
    public:
        LBMStreamEntry(const packet_info * pinfo, uint64_t channel, const lbm_uim_stream_endpoint_t * endpoint_a, const lbm_uim_stream_endpoint_t * endpoint_b);
        ~LBMStreamEntry(void);
        void processPacket(const packet_info * pinfo, const lbm_uim_stream_tap_info_t * stream_info);
        void setItem(QTreeWidgetItem * item);
        QTreeWidgetItem * getItem(void)
        {
            return (m_item);
        }

    private:
        void fillItem(bool update_only = true);
        QString formatEndpoint(const packet_info * pinfo, const lbm_uim_stream_endpoint_t * endpoint);
        uint64_t m_channel;
        QString m_endpoint_a;
        QString m_endpoint_b;
        uint32_t m_first_frame;
        uint32_t m_flast_frame;
        uint32_t m_messages;
        uint32_t m_bytes;
        QTreeWidgetItem * m_item;
        LBMSubstreamMap m_substreams;
};

LBMStreamEntry::LBMStreamEntry(const packet_info * pinfo, uint64_t channel, const lbm_uim_stream_endpoint_t * endpoint_a, const lbm_uim_stream_endpoint_t * endpoint_b) :
    m_channel(channel),
    m_first_frame((uint32_t)(~0)),
    m_flast_frame(0),
    m_messages(0),
    m_bytes(0),
    m_item(NULL),
    m_substreams()
{
    m_endpoint_a = formatEndpoint(pinfo, endpoint_a);
    m_endpoint_b = formatEndpoint(pinfo, endpoint_b);
}

LBMStreamEntry::~LBMStreamEntry(void)
{
    LBMSubstreamMapIterator it;

    for (it = m_substreams.begin(); it != m_substreams.end(); ++it)
    {
        delete *it;
    }
    m_substreams.clear();
}

QString LBMStreamEntry::formatEndpoint(const packet_info * pinfo, const lbm_uim_stream_endpoint_t * endpoint)
{
    if (endpoint->type == lbm_uim_instance_stream)
    {
        return QString(bytes_to_str(pinfo->pool, endpoint->stream_info.ctxinst.ctxinst, sizeof(endpoint->stream_info.ctxinst.ctxinst)));
    }
    else
    {
        return QStringLiteral("%1:%2:%3")
               .arg(endpoint->stream_info.dest.domain)
               .arg(address_to_str(pinfo->pool, &(endpoint->stream_info.dest.addr)))
               .arg(endpoint->stream_info.dest.port);
    }
}

void LBMStreamEntry::processPacket(const packet_info * pinfo, const lbm_uim_stream_tap_info_t * stream_info)
{
    LBMSubstreamEntry * substream = NULL;
    LBMSubstreamMapIterator it;

    if (m_first_frame > pinfo->num)
    {
        m_first_frame = pinfo->num;
    }
    if (m_flast_frame < pinfo->num)
    {
        m_flast_frame = pinfo->num;
    }
    m_bytes += stream_info->bytes;
    m_messages++;
    it = m_substreams.find(stream_info->substream_id);
    if (m_substreams.end() == it)
    {
        QTreeWidgetItem * item = NULL;

        substream = new LBMSubstreamEntry(m_channel, stream_info->substream_id, &(pinfo->src), pinfo->srcport, &(pinfo->dst), pinfo->destport);
        m_substreams.insert(stream_info->substream_id, substream);
        item = new QTreeWidgetItem();
        substream->setItem(item);
        m_item->addChild(item);
        m_item->sortChildren(Stream_Column, Qt::AscendingOrder);
    }
    else
    {
        substream = it.value();
    }
    fillItem();
    substream->processPacket(pinfo->num, stream_info->bytes);
}

void LBMStreamEntry::setItem(QTreeWidgetItem * item)
{
    m_item = item;
    fillItem(false);
}

void LBMStreamEntry::fillItem(bool update_only)
{
    if (update_only == false)
    {
        m_item->setData(Stream_Column, Qt::DisplayRole, QVariant((qulonglong)m_channel));
        m_item->setText(EndpointA_Column, m_endpoint_a);
        m_item->setText(EndpointB_Column, m_endpoint_b);
    }
    m_item->setText(Messages_Column, QStringLiteral("%1").arg(m_messages));
    m_item->setText(Bytes_Column, QStringLiteral("%1").arg(m_bytes));
    m_item->setText(FirstFrame_Column, QStringLiteral("%1").arg(m_first_frame));
    m_item->setText(LastFrame_Column, QStringLiteral("%1").arg(m_flast_frame));
}

typedef QMap<uint64_t, LBMStreamEntry *> LBMStreamMap;
typedef QMap<uint64_t, LBMStreamEntry *>::iterator LBMStreamMapIterator;

class LBMStreamDialogInfo
{
    public:
        LBMStreamDialogInfo(void);
        ~LBMStreamDialogInfo(void);
        void setDialog(LBMStreamDialog * dialog);
        LBMStreamDialog * getDialog(void);
        void processPacket(const packet_info * pinfo, const lbm_uim_stream_tap_info_t * stream_info);
        void resetStreams(void);

    private:
        LBMStreamDialog * m_dialog;
        LBMStreamMap m_streams;
};

LBMStreamDialogInfo::LBMStreamDialogInfo(void) :
    m_dialog(NULL),
    m_streams()
{
}

LBMStreamDialogInfo::~LBMStreamDialogInfo(void)
{
    resetStreams();
}

void LBMStreamDialogInfo::setDialog(LBMStreamDialog * dialog)
{
    m_dialog = dialog;
}

LBMStreamDialog * LBMStreamDialogInfo::getDialog(void)
{
    return (m_dialog);
}

void LBMStreamDialogInfo::processPacket(const packet_info * pinfo, const lbm_uim_stream_tap_info_t * stream_info)
{
    LBMStreamEntry * stream = NULL;
    LBMStreamMapIterator it;

    it = m_streams.find(stream_info->channel);
    if (m_streams.end() == it)
    {
        QTreeWidgetItem * item = NULL;
        QTreeWidgetItem * parent = NULL;
        Ui::LBMStreamDialog * ui = NULL;

        stream = new LBMStreamEntry(pinfo, stream_info->channel, &(stream_info->endpoint_a), &(stream_info->endpoint_b));
        it = m_streams.insert(stream_info->channel, stream);
        item = new QTreeWidgetItem();
        stream->setItem(item);
        ui = m_dialog->getUI();
        ui->lbm_stream_TreeWidget->addTopLevelItem(item);
        parent = ui->lbm_stream_TreeWidget->invisibleRootItem();
        parent->sortChildren(Stream_Column, Qt::AscendingOrder);
    }
    else
    {
        stream = it.value();
    }
    stream->processPacket(pinfo, stream_info);
}

void LBMStreamDialogInfo::resetStreams(void)
{
    LBMStreamMapIterator it = m_streams.begin();

    while (it != m_streams.end())
    {
        delete *it;
        ++it;
    }
    m_streams.clear();
}

LBMStreamDialog::LBMStreamDialog(QWidget * parent, capture_file * cfile) :
    QDialog(parent),
    m_ui(new Ui::LBMStreamDialog),
    m_dialog_info(NULL),
    m_capture_file(cfile)
{
    m_ui->setupUi(this);
    m_dialog_info = new LBMStreamDialogInfo();
    setAttribute(Qt::WA_DeleteOnClose, true);
    fillTree();
}

LBMStreamDialog::~LBMStreamDialog(void)
{
    delete m_ui;
    if (m_dialog_info != NULL)
    {
        delete m_dialog_info;
    }
}

void LBMStreamDialog::setCaptureFile(capture_file * cfile)
{
    if (cfile == NULL) // We only want to know when the file closes.
    {
        m_capture_file = NULL;
        m_ui->displayFilterLineEdit->setEnabled(false);
        m_ui->applyFilterButton->setEnabled(false);
    }
}

void LBMStreamDialog::fillTree(void)
{
    GString * error_string;

    if (m_capture_file == NULL)
    {
        return;
    }
    m_dialog_info->setDialog(this);

    error_string = register_tap_listener("lbm_stream",
        (void *)m_dialog_info,
        m_ui->displayFilterLineEdit->text().toUtf8().constData(),
        TL_REQUIRES_COLUMNS,
        resetTap,
        tapPacket,
        drawTreeItems,
        NULL);
    if (error_string)
    {
        QMessageBox::critical(this, tr("LBM Stream failed to attach to tap"),
            error_string->str);
        g_string_free(error_string, TRUE);
        reject();
    }

    cf_retap_packets(m_capture_file);
    drawTreeItems(&m_dialog_info);
    remove_tap_listener((void *)m_dialog_info);
}

void LBMStreamDialog::resetTap(void * tap_data)
{
    LBMStreamDialogInfo * info = (LBMStreamDialogInfo *)tap_data;
    LBMStreamDialog * dialog = info->getDialog();
    if (dialog == NULL)
    {
        return;
    }
    info->resetStreams();
    dialog->m_ui->lbm_stream_TreeWidget->clear();
}

tap_packet_status LBMStreamDialog::tapPacket(void * tap_data, packet_info * pinfo, epan_dissect_t *, const void * stream_info, tap_flags_t)
{
    if (pinfo->fd->passed_dfilter == 1)
    {
        const lbm_uim_stream_tap_info_t * tapinfo = (const lbm_uim_stream_tap_info_t *)stream_info;
        LBMStreamDialogInfo * info = (LBMStreamDialogInfo *)tap_data;

        info->processPacket(pinfo, tapinfo);
    }
    return (TAP_PACKET_REDRAW);
}

void LBMStreamDialog::drawTreeItems(void *)
{
}

void LBMStreamDialog::on_applyFilterButton_clicked(void)
{
    fillTree();
}
