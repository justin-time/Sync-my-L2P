#include "parser.h"

#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>

#include "utils.h"
#include "qslog/QsLog.h"

QList<QString> Parser::parseFeatures(QNetworkReply *reply)
{
    // Empfangene Nachricht auslesen und als JSON interpretieren
    QByteArray response(reply->readAll());
    QJsonDocument document = QJsonDocument::fromJson(response);
    QJsonObject object = document.object();

    QList<QString> activeFeatures;

    if(object.isEmpty())
    {
        QLOG_INFO() << tr("Featureinformationen leer bzw. nicht lesbar.");
        return activeFeatures;
    }

    if(!object["Status"].toBool())
    {
        QLOG_ERROR() << tr("Status der Featureinformationen nicht ok: ") <<
                        QString(document.toJson());
        return activeFeatures;
    }

    // Array mit allen einzelnen Vorlesungen/Veranstaltungen
    QJsonArray courses = object["active"].toArray();

    // Für jede Veranstaltung ein neues Strukturelement anlegen
    foreach(QJsonValue element, courses)
    {
        activeFeatures << element.toString();
    }

    return activeFeatures;
}

void Parser::parseCourses(QNetworkReply *reply, QStandardItemModel *itemModel)
{
    // Empfangene Nachricht auslesen und als JSON interpretieren
    QByteArray response(reply->readAll());
    QJsonDocument document = QJsonDocument::fromJson(response);
    QJsonObject object = document.object();

    if(object.isEmpty())
    {
        QLOG_WARN() << tr("Kursinformationen leer bzw. nicht lesbar.");
        return;
    }

    if(!object["Status"].toBool())
    {
        QLOG_ERROR() << tr("Status der Kursinformationen nicht ok: ") <<
                        QString(document.toJson());
        return;
    }

    // Array mit allen einzelnen Vorlesungen/Veranstaltungen
    QJsonArray courses = object["dataSet"].toArray();

    // Für jede Veranstaltung ein neues Strukturelement anlegen
    foreach(QJsonValue element, courses)
    {
        QJsonObject course = element.toObject();

        QString title = course["courseTitle"].toString();
        QString cid = course["uniqueid"].toString();
        QString semester = course["semester"].toString();
        QString url = course["url"].toString();

        // Erstellen eines RegExps  für unzulässige Buchstaben im Veranstaltungsnamen
        QString escapePattern = "(:|<|>|/|\\\\|\\||\\*|\\^|\\?|\\\")";
        QRegExp escapeRegExp(escapePattern, Qt::CaseSensitive);
        title = title.replace(escapeRegExp, "").trimmed();
        // Titellänge limitieren um Probleme bei Dateisystemen zu verhindern
        title.truncate(100);

        Structureelement *newCourse = new Structureelement(title, QUrl(url), 0, 0, cid, courseItem);

        Utils::getSemesterItem(itemModel, semester)->appendRow(newCourse);

        QLOG_DEBUG() << tr("Veranstaltung") << title << "(" << cid << tr(") hinzugefügt.");
    }
}

void Parser::parseFiles(QNetworkReply *reply, Structureelement* course, QString downloadDirectoryPath)
{
    QString url = reply->url().toString();

    int responseCategory;

    // Kategorie der Daten bestimmen, um richtigen Parser auszuwählen
    if(url.contains("viewAllLearningMaterials"))
    {
        responseCategory = 0;
    }
    else if(url.contains("viewAllSharedDocuments"))
    {
        responseCategory = 1;
    }
    else if(url.contains("viewAllAssignments"))
    {
        responseCategory = 2;
    }
    else if(url.contains("viewAllMediaLibraries"))
    {
        responseCategory = 3;
    }
    else if(url.contains("viewAllAnnouncements"))
    {
        responseCategory = 4;
    }
    else if(url.contains("viewAllEmails"))
    {
        responseCategory = 5;
    }
    else if(url.contains("viewAllWikis"))
    {
        responseCategory = 6;
    }
    else
    {
        QLOG_ERROR() << tr("Antwort auf unbekannten Request erhalten: ") << url;
        return;
    }

    Structureelement *currentCourse = course;

    QByteArray response = reply->readAll();

    QJsonDocument document = QJsonDocument::fromJson(response);
    QJsonObject object = document.object();

    if(object.isEmpty())
    {
        QLOG_DEBUG() << tr("Kursinformationen leer bzw. nicht lesbar.");
        return;
    }

    if(!object["Status"].toBool())
    {
        if( url.contains( "viewAllAssignments") )
        {
            return;
        }

        QLOG_ERROR() << tr("Status der Kursinformationen nicht ok: \n") <<
                        url << "\n" <<
                        QString(document.toJson());
        return;
    }

    QJsonArray files = object["dataSet"].toArray();
    foreach(QJsonValue element, files)
    {
        QJsonObject file = element.toObject();
        QString filename;
        int filesize;
        int timestamp;
        QString url;
        QStringList urlParts;

        // Skip directories
        if(file["isDirectory"].toBool())
        {
            continue;
        }

        if(responseCategory == 0 ||responseCategory == 1 || responseCategory == 3)
        {
            QJsonObject fileInformation = file["fileInformation"].toObject();

            filename = fileInformation["fileName"].toString();
            filesize = fileInformation["fileSize"].toString().toInt();
            timestamp = fileInformation["modifiedTimestamp"].toInt();
            url = fileInformation["downloadUrl"].toString();
            url = QByteArray::fromPercentEncoding(url.toLocal8Bit());
            urlParts = url.split('/');

            // Wir brauchen keine Vorschaubilder
            if(url.contains("Preview Images"))
                {
                    continue;
                }

            urlParts.removeFirst();
            urlParts.removeFirst();
            urlParts.removeFirst();
            urlParts.removeFirst();
            if (responseCategory == 1){
                urlParts.removeFirst();
            }
            urlParts.removeLast();

            Structureelement *dir = Utils::getDirectoryItem(currentCourse, urlParts);

            Structureelement* newFile = new Structureelement(filename, QUrl(url), timestamp, filesize,
                                                         currentCourse->data(cidRole).toString(),
                                                         fileItem);

            // Element hinzufügen
            dir->appendRow(newFile);
        }
        else if(responseCategory == 2)
        {
            // Läd das Übungsdokument herunter
            QJsonArray assignmentDocs = file["assignmentDocuments"].toArray();
            foreach(QJsonValue assignmentElement, assignmentDocs)
            {
                QJsonObject assignmentDoc = assignmentElement.toObject();

                filename = assignmentDoc["fileName"].toString();
                filesize = assignmentDoc["fileSize"].toString().toInt();
                timestamp = assignmentDoc["modifiedTimestamp"].toInt();
                url = assignmentDoc["downloadUrl"].toString();
                url = QByteArray::fromPercentEncoding(url.toLocal8Bit());
                urlParts = url.split('/');

                urlParts.removeFirst();
                urlParts.removeFirst();
                urlParts.removeFirst();
                urlParts.removeFirst();
                urlParts.removeFirst();
                urlParts.removeLast();

                // Fix Browser-URL
                url.remove(0,10);

                Structureelement *dir = Utils::getDirectoryItem(currentCourse, urlParts);

                Structureelement* newFile = new Structureelement(filename, QUrl(url), timestamp, filesize,
                                                                 currentCourse->data(cidRole).toString(),
                                                                 fileItem);

                // Element hinzufügen
                dir->appendRow(newFile);
            }

            // Läd die Korrektur der eingereichten Lösung herunter
            QJsonObject correction = file["correction"].toObject();
            QJsonArray correctionFiles= correction["correctionDocuments"].toArray();
            foreach(QJsonValue correctionElement, correctionFiles)
            {
                QJsonObject correctionDoc = correctionElement.toObject();

                filename = correctionDoc["fileName"].toString();
                filesize = correctionDoc["fileSize"].toString().toInt();
                timestamp = correctionDoc["modifiedTimestamp"].toInt();
                url = correctionDoc["downloadUrl"].toString();
                url = QByteArray::fromPercentEncoding(url.toLocal8Bit());
                urlParts = url.split('/');

                urlParts.removeFirst();
                urlParts.removeFirst();
                urlParts.removeFirst();
                urlParts.removeFirst();
                urlParts.removeFirst();
                urlParts.removeLast();
                urlParts.removeLast();

                // Fix Browser-URL
                url.remove(0,10);

                Structureelement *dir = Utils::getDirectoryItem(currentCourse, urlParts);

                Structureelement* newFile = new Structureelement(filename, QUrl(url), timestamp, filesize,
                                                                 currentCourse->data(cidRole).toString(),
                                                                 fileItem);
                // Element hinzufügen
                dir->appendRow(newFile);
             }

            // Läd die eingereichten Lösungen herunter
            QJsonObject solutions = file["solution"].toObject();
            QJsonArray solutionFiles= solutions["solutionDocuments"].toArray();
            foreach(QJsonValue solutionElement, solutionFiles)
            {
                QJsonObject solutionDoc = solutionElement.toObject();

                filename = solutionDoc["fileName"].toString();
                filesize = solutionDoc["fileSize"].toString().toInt();
                timestamp = solutionDoc["modifiedTimestamp"].toInt();
                url = solutionDoc["downloadUrl"].toString();
                url = QByteArray::fromPercentEncoding(url.toLocal8Bit());
                urlParts = url.split('/');

                urlParts.removeFirst();
                urlParts.removeFirst();
                urlParts.removeFirst();
                urlParts.removeFirst();
                urlParts.removeFirst();
                urlParts.removeLast();
                urlParts.removeLast();

                // Fix Browser-URL
                url.remove(0,10);

                Structureelement *dir = Utils::getDirectoryItem(currentCourse, urlParts);

                Structureelement* newFile = new Structureelement(filename, QUrl(url), timestamp, filesize,
                                                                 currentCourse->data(cidRole).toString(),
                                                                 fileItem);
                // Element hinzufügen
                dir->appendRow(newFile);
             }

            // Läd die Musterlösung herunter
            QJsonArray sampleSolutions = file["SampleSolutionDocuments"].toArray();
            foreach(QJsonValue sampleSolutionElement, sampleSolutions)
            {
                QJsonObject sampleSolutionDoc = sampleSolutionElement.toObject();

                filename = sampleSolutionDoc["fileName"].toString();
                filesize = sampleSolutionDoc["fileSize"].toString().toInt();
                timestamp = sampleSolutionDoc["modifiedTimestamp"].toInt();
                url = sampleSolutionDoc["downloadUrl"].toString();
                url = QByteArray::fromPercentEncoding(url.toLocal8Bit());
                urlParts = url.split('/');

                urlParts.removeFirst();
                urlParts.removeFirst();
                urlParts.removeFirst();
                urlParts.removeFirst();
                urlParts.removeFirst();
                urlParts.removeLast();

                // Fix Browser-URL
                url.remove(0,10);

                Structureelement *dir = Utils::getDirectoryItem(currentCourse, urlParts);

                Structureelement* newFile = new Structureelement(filename, QUrl(url), timestamp, filesize,
                                                                 currentCourse->data(cidRole).toString(),
                                                                 fileItem);

                // Element hinzufügen
                dir->appendRow(newFile);
            }

        }
        else if(responseCategory == 4)
        {
            QString body = file["body"].toString();
            QString title = file["title"].toString();
            QString from = tr("Ankündigung im L2P");
            int time = file["modifiedTimestamp"].toInt();
            QString dirname = "Announcement";
            urlParts.append(dirname);

            Structureelement *dir = Utils::getDirectoryItem(currentCourse, urlParts);

            Structureelement* newMessage = new Structureelement(body, title, from, time,
                                                             currentCourse->data(cidRole).toString(),
                                                             messageItem);
            dir->appendRow(newMessage);

            if (!file["attachments"].isNull()) {
                QJsonArray attachment = file["attachments"].toArray();
                foreach(QJsonValue attachmentElement, attachment)
                {
                    QJsonObject fileInformation = attachmentElement.toObject();
                    filename = fileInformation["fileName"].toString();
                    filesize = fileInformation["fileSize"].toString().toInt();
                    timestamp = fileInformation["modifiedTimestamp"].toInt();
                    url = fileInformation["downloadUrl"].toString();
                    urlParts = url.split('/');
                    urlParts.removeFirst();
                    urlParts.removeFirst();
                    urlParts.removeFirst();
                    urlParts.removeFirst();
                    urlParts.removeLast();
                    urlParts.removeLast();

                    Structureelement *dir = Utils::getDirectoryItem(currentCourse, urlParts);

                    Structureelement* newFile = new Structureelement(filename, QUrl(url), timestamp, filesize,
                                                                     currentCourse->data(cidRole).toString(),
                                                                     fileItem);

                    // Element hinzufügen
                    dir->appendRow(newFile);

                }
            }
            else {
                continue;
            }
        }
        else if(responseCategory == 5)
        {
            QString body = file["body"].toString();
            QString title = file["subject"].toString();
            QString from = file["from"].toString();
            int time = file["modifiedTimestamp"].toInt();
            QString dirname = "E-Mails";
            urlParts.append(dirname);


            Structureelement *dir = Utils::getDirectoryItem(currentCourse, urlParts);

            Structureelement* newMessage = new Structureelement(body, title, from, time,
                                                             currentCourse->data(cidRole).toString(),
                                                             messageItem);
            dir->appendRow(newMessage);


            if (!file["attachments"].isNull()) {
                QJsonArray attachment = file["attachments"].toArray();
                foreach(QJsonValue attachmentElement, attachment)
                {
                    QJsonObject fileInformation = attachmentElement.toObject();
                    filename = fileInformation["fileName"].toString();
                    filesize = fileInformation["fileSize"].toString().toInt();
                    timestamp = fileInformation["modifiedTimestamp"].toInt();
                    url = fileInformation["downloadUrl"].toString();
                    urlParts = url.split('/');
                    urlParts.removeFirst();
                    urlParts.removeFirst();
                    urlParts.removeFirst();
                    urlParts.removeFirst();
                    urlParts.removeLast();
                    urlParts.removeLast();

                    Structureelement *dir = Utils::getDirectoryItem(currentCourse, urlParts);

                    Structureelement* newFile = new Structureelement(filename, QUrl(url), timestamp, filesize,
                                                                     currentCourse->data(cidRole).toString(),
                                                                     fileItem);

                    // Element hinzufügen
                    dir->appendRow(newFile);


                }
            }
        }
        else if(responseCategory == 6)
        {
            int bodyId = file["itemId"].toInt();
            QString title = file["title"].toString();
            QString from = file["authors"].toString();
            int time = file["lastModified"].toInt();
            QString dirname = "Wiki";
            urlParts.append(dirname);


            Structureelement *dir = Utils::getDirectoryItem(currentCourse, urlParts);

            Structureelement* newWiki = new Structureelement(bodyId, title, from, time,
                                                                currentCourse->data(cidRole).toString(),
                                                                wikiItem);
            dir->appendRow(newWiki);
                }
         else {
                continue;
        }

 }
}
