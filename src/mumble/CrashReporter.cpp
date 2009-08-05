/* Copyright (C) 2009, Mikkel Krautz <mikkel@krautz.dk>

   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.
   - Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.
   - Neither the name of the Mumble Developers nor the names of its
     contributors may be used to endorse or promote products derived from this
     software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "CrashReporter.h"
#include "Global.h"
#include "OSInfo.h"

CrashReporter::CrashReporter(QWidget *p) : QDialog(p) {
	setWindowTitle(tr("Mumble Crash Report"));

	QVBoxLayout *vbl= new QVBoxLayout(this);

	QLabel *l;

	l = new QLabel(tr("<p><b>We're terribly sorry, but it seems Mumble has crashed. Do you want to send a crash report to the Mumble developers?</b></p>"
	                  "<p>The crash report contains a partial copy of Mumble's memory at the time it crashed, and will help the developers fix the problem.</p>"));

	vbl->addWidget(l);

	QHBoxLayout *hbl = new QHBoxLayout();

	qleEmail = new QLineEdit(g.qs->value(QLatin1String("crashemail")).toString());
	l = new QLabel(tr("Email address (optional)"));
	l->setBuddy(qleEmail);

	hbl->addWidget(l);
	hbl->addWidget(qleEmail, 1);
	vbl->addLayout(hbl);

	qteDescription=new QTextEdit();
	l->setBuddy(qteDescription);
	l = new QLabel(tr("Please briefly describe what you were doing at the time of the crash"));

	vbl->addWidget(l);
	vbl->addWidget(qteDescription, 1);

	QPushButton *pbOk = new QPushButton(tr("Send Report"));
	pbOk->setDefault(true);

	QPushButton *pbCancel = new QPushButton(tr("Don't send report"));
	pbCancel->setAutoDefault(false);

	QDialogButtonBox *dbb = new QDialogButtonBox(Qt::Horizontal);
	dbb->addButton(pbOk, QDialogButtonBox::AcceptRole);
	dbb->addButton(pbCancel, QDialogButtonBox::RejectRole);
	connect(dbb, SIGNAL(accepted()), this, SLOT(accept()));
	connect(dbb, SIGNAL(rejected()), this, SLOT(reject()));
	vbl->addWidget(dbb);

	qelLoop = new QEventLoop(this);
	qpdProgress = NULL;
	qnrReply = NULL;
}

CrashReporter::~CrashReporter() {
	g.qs->setValue(QLatin1String("crashemail"), qleEmail->text());
	if (qnrReply)
		delete qnrReply;
}

void CrashReporter::uploadFinished() {
	qpdProgress->reset();
	if (qnrReply->error() == QNetworkReply::NoError) {
		if (qnrReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200)
			QMessageBox::information(NULL, tr("Crash upload successfull"), tr("Thank you for helping make Mumble better!"));
		else
			QMessageBox::critical(NULL, tr("Crash upload failed"), tr("We're really sorry, but it appears the crash upload has failed with error %1 %2. Please inform a developer.").arg(qnrReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()).arg(qnrReply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString()));
	} else {
		QMessageBox::critical(NULL, tr("Crash upload failed"), tr("This really isn't funny, but apparantly there's a bug in the crash reporting code, and we've failed to upload the report. You may inform a developer about error %1").arg(qnrReply->error()));
	}
	qelLoop->exit(0);
}

void CrashReporter::uploadProgress(qint64 sent, qint64 total) {
	qpdProgress->setMaximum(total);
	qpdProgress->setValue(sent);
}

void CrashReporter::run() {


	QFile qfMinidump(g.qdBasePath.filePath(QLatin1String("mumble.dmp")));
	if (! qfMinidump.exists())
		return;

	qfMinidump.open(QIODevice::ReadOnly);

#ifdef Q_OS_WIN
	if (qfMinidump.peek(4) != "MDMP")
		return;
#endif

	if (exec() == QDialog::Accepted) {
		QByteArray dump = qfMinidump.readAll();

		qpdProgress = new QProgressDialog(tr("Uploading crash report"), tr("Abort upload"), 0, 100, this);
		qpdProgress->setMinimumDuration(500);
		qpdProgress->setValue(0);
		connect(qpdProgress, SIGNAL(canceled()), qelLoop, SLOT(quit()));

		QString boundary = QString::fromLatin1("---------------------------%1").arg(QDateTime::currentDateTime().toTime_t());

		QString os = QString::fromLatin1("--%1\r\nContent-Disposition: form-data; name=\"os\"\r\nContent-Transfer-Encoding: 8bit\r\n\r\n%2 %3\r\n").arg(boundary, OSInfo::getOS(), OSInfo::getOSVersion());
		QString ver = QString::fromLatin1("--%1\r\nContent-Disposition: form-data; name=\"ver\"\r\nContent-Transfer-Encoding: 8bit\r\n\r\n%2 %3\r\n").arg(boundary, QLatin1String(MUMTEXT(MUMBLE_VERSION_STRING)), QLatin1String(MUMBLE_RELEASE));
		QString email = QString::fromLatin1("--%1\r\nContent-Disposition: form-data; name=\"email\"\r\nContent-Transfer-Encoding: 8bit\r\n\r\n%2\r\n").arg(boundary, qleEmail->text());
		QString descr = QString::fromLatin1("--%1\r\nContent-Disposition: form-data; name=\"desc\"\r\nContent-Transfer-Encoding: 8bit\r\n\r\n%2\r\n").arg(boundary, qteDescription->toPlainText());
		QString head = QString::fromLatin1("--%1\r\nContent-Disposition: form-data; name=\"dump\"; filename=\"mumble.dmp\"\r\nContent-Type: binary/octet-stream\r\n\r\n").arg(boundary);
		QString end = QString::fromLatin1("\r\n--%1--\r\n").arg(boundary);

		QByteArray post = os.toUtf8() + ver.toUtf8() + email.toUtf8() + descr.toUtf8() + head.toUtf8() + dump + end.toUtf8();

		QUrl url(QLatin1String("https://mumble.hive.no/crashreport.php"));
		QNetworkRequest req(url);
		req.setHeader(QNetworkRequest::ContentTypeHeader, QString::fromLatin1("multipart/form-data; boundary=%1").arg(boundary));
		req.setHeader(QNetworkRequest::ContentLengthHeader, QString::number(post.size()));
		qnrReply = g.nam->post(req, post);
		connect(qnrReply, SIGNAL(finished()), this, SLOT(uploadFinished()));
		connect(qnrReply, SIGNAL(uploadProgress(qint64, qint64)), this, SLOT(uploadProgress(qint64, qint64)));

		qelLoop->exec(QEventLoop::DialogExec);
	}

	if (! qfMinidump.remove())
		qWarning("CrashReporeter: Unable to remove minidump.");
}
