/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright 2012 The MITRE Corporation                                      *
 *                                                                           *
 * Licensed under the Apache License, Version 2.0 (the "License");           *
 * you may not use this file except in compliance with the License.          *
 * You may obtain a copy of the License at                                   *
 *                                                                           *
 *     http://www.apache.org/licenses/LICENSE-2.0                            *
 *                                                                           *
 * Unless required by applicable law or agreed to in writing, software       *
 * distributed under the License is distributed on an "AS IS" BASIS,         *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  *
 * See the License for the specific language governing permissions and       *
 * limitations under the License.                                            *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "plot.h"
#include "version.h"
#include "openbr/core/qtutils.h"

using namespace cv;

namespace br
{

static QStringList getPivots(const QString &file, bool headers)
{
    QString str;
    if (headers) str = QFileInfo(file).dir().dirName();
    else         str = QFileInfo(file).completeBaseName();
    return str.split("_");
}

static QString getScale(const QString &mode, const QString &title, int vals)
{
    if      (vals > 12) return " + scale_"+mode+"_discrete(\""+title+"\")";
    else if (vals > 11) return " + scale_"+mode+"_brewer(\""+title+"\", palette=\"Set3\")";
    else if (vals > 9)  return " + scale_"+mode+"_brewer(\""+title+"\", palette=\"Paired\")";
    else                return " + scale_"+mode+"_brewer(\""+title+"\", palette=\"Set1\")";
}

// Custom sorting method to ensure datasets are ordered nicely
static bool sortFiles(const QString &fileA, const QString &fileB)
{
    return fileA < fileB;
}

struct RPlot
{
    QString basename, suffix;
    QFile file;
    QStringList pivotHeaders;
    QVector< QSet<QString> > pivotItems;
    float confidence; // confidence interval for plotting across splits
    int ncol;         // Number of columns for plot legends

    bool flip;

    struct Pivot
    {
        int index, size;
        QString header;
        bool smooth;
        Pivot() : index(-1), size(0), smooth(false) {}
        Pivot(int _index, int _size, const QString &_header)
            : index(_index), size(_size), header(_header), smooth(false) {}
    };

    Pivot major, minor;

    RPlot(QStringList files, const File &destination, bool isEvalFormat = true)
    {
        if (files.isEmpty()) qFatal("Empty file list.");
        qSort(files.begin(), files.end(), sortFiles);

        // Parse destination
        QFileInfo fileInfo(destination);
        basename = fileInfo.path() + "/" + fileInfo.completeBaseName();
        suffix = fileInfo.suffix();
        if (suffix.isEmpty()) suffix = "pdf";

        file.setFileName(basename+".R");
        bool success = file.open(QFile::WriteOnly);
        if (!success) qFatal("Failed to open %s for writing.", qPrintable(file.fileName()));

        file.write("# Load libraries\n"
                   "library(ggplot2)\n"
                   "library(gplots)\n"
                   "library(reshape)\n"
                   "library(scales)\n"
                   "\n"
                   "# Read CSVs\n"
                   "data <- NULL\n");

        // Read files and retrieve pivots
        pivotHeaders = getPivots(files.first(), true);
        pivotItems = QVector< QSet<QString> >(pivotHeaders.size());
        foreach (const QString &fileName, files) {
            QStringList pivots = getPivots(fileName, false);
            // If the number of pivots don't match, abandon the directory/filename labeling scheme
            if (pivots.size() != pivotHeaders.size()) {
                pivots.clear();
                pivots.push_back(QFileInfo(fileName).completeBaseName());
                pivotHeaders.clear();
                pivotHeaders.push_back("File");
            } 
            file.write(qPrintable(QString("tmp <- read.csv(\"%1\")\n").arg(fileName).replace("\\", "\\\\")));
            for (int i=0; i<pivots.size(); i++) {
                pivotItems[i].insert(pivots[i]);
                file.write(qPrintable(QString("tmp$%1 <- \"%2\"\n").arg(pivotHeaders[i], pivots[i])));
            }
            file.write("data <- rbind(data, tmp)\n");
        }

        for (int i=0; i<pivotItems.size(); i++) {
            const int size = pivotItems[i].size();
            if (size > major.size) {
                minor = major;
                major = Pivot(i, size, pivotHeaders[i]);
            } else if (size > minor.size) {
                minor = Pivot(i, size, pivotHeaders[i]);
            }
        }

        const QString &smooth = destination.get<QString>("smooth", "");
        confidence = destination.get<float>("confidence", 95) / 100.0;

        major.smooth = !smooth.isEmpty() && (major.header == smooth) && (major.size > 1);
        minor.smooth = !smooth.isEmpty() && (minor.header == smooth) && (minor.size > 1);
        if (major.smooth) major.size = 1;
        if (minor.smooth) minor.size = 1;
        if (major.size < minor.size)
            std::swap(major, minor);

        ncol = destination.get<int>("ncol", major.size > 1 ? major.size : (minor.header.isEmpty() ? major.size : minor.size));
        flip = minor.header == "Algorithm";
        // Format data
        if (isEvalFormat)
            file.write(qPrintable(QString("\n"
                       "# Split data into individual plots\n"
                       "plot_index = which(names(data)==\"Plot\")\n"
                       "Metadata <- data[grep(\"Metadata\",data$Plot),-c(1)]\n"
                       "IM <- data[grep(\"IM\",data$Plot),-c(1)]\n"
                       "GM <- data[grep(\"GM\",data$Plot),-c(1)]\n"
                       "DET <- data[grep(\"DET\",data$Plot),-c(1)]\n"
                       "IET <- data[grep(\"IET\",data$Plot),-c(1)]\n"
                       "FAR <- data[grep(\"FAR\",data$Plot),-c(1)]\n"
                       "FRR <- data[grep(\"FRR\",data$Plot),-c(1)]\n"
                       "SD <- data[grep(\"SD\",data$Plot),-c(1)]\n"
                       "TF <- data[grep(\"TF\",data$Plot),-c(1)]\n"
                       "FT <- data[grep(\"FT\",data$Plot),-c(1)]\n"
                       "CT <- data[grep(\"CT\",data$Plot),-c(1)]\n"
                       "BC <- data[grep(\"BC\",data$Plot),-c(1)]\n"
                       "TS <- data[grep(\"TS\",data$Plot),-c(1)]\n"
                       "CMC <- data[grep(\"CMC\",data$Plot),-c(1)]\n"
                       "FAR$Error <- \"FAR\"\n"
                       "FRR$Error <- \"FRR\"\n"
                       "ERR <- rbind(FAR, FRR)\n"
                       "rm(data, FAR, FRR)\n"
                       "\n"
                       "# Format data\n"
                       "Metadata$Y<-factor(Metadata$Y, levels=c(\"Genuine\",\"Impostor\",\"Ignored\",\"Gallery\",\"Probe\"))\n"
                       "IM$Y <- as.character(IM$Y)\n"
                       "GM$Y <- as.character(GM$Y)\n"
                       "DET$Y <- as.numeric(as.character(DET$Y))\n"
                       "IET$Y <- as.numeric(as.character(IET$Y))\n"
                       "ERR$Y <- as.numeric(as.character(ERR$Y))\n"
                       "SD$Y <- as.factor(unique(as.character(SD$Y)))\n"
                       "TF$Y <- as.numeric(as.character(TF$Y))\n"
                       "FT$Y <- as.numeric(as.character(FT$Y))\n"
                       "CT$Y <- as.numeric(as.character(CT$Y))\n"
                       "BC$Y <- as.numeric(as.character(BC$Y))\n"
                       "TS$Y <- as.character(TS$Y)\n"
                       "CMC$Y <- as.numeric(as.character(CMC$Y))\n"
                       "\n"
                       "if (%1) {\n\tsummarySE <- function(data=NULL, measurevar, groupvars=NULL, na.rm=FALSE, conf.interval=%3, .drop=TRUE) {\n\t\t"
                       "require(plyr)\n\n\t\tlength2 <- function (x, na.rm=FALSE) {\n\t\t\tif (na.rm) sum(!is.na(x))\n\t\t\telse       length(x)"
                       "\n\t\t}\n\n\t\tdatac <- ddply(data, groupvars, .drop=.drop, .fun = function(xx, col) {\n\t\t\t"
                       "c(N=length2(xx[[col]], na.rm=na.rm), mean=mean(xx[[col]], na.rm=na.rm), sd=sd(xx[[col]], na.rm=na.rm))\n\t\t\t},"
                       "\n\t\t\tmeasurevar\n\t\t)\n\n\t\tdatac <- rename(datac, c(\"mean\" = measurevar))\n\t\tdatac$se <- datac$sd / sqrt(datac$N)"
                       "\n\t\tciMult <- qt(conf.interval/2 + .5, datac$N-1)\n\t\tdatac$ci <- datac$se * ciMult\n\n\t\t"
                       "datac$upper <- if(datac[, measurevar] + datac$ci < 1) (datac[, measurevar] + datac$ci) else 1\n\t\t"
                       "datac$lower <- if(datac[, measurevar] - datac$ci > 0) (datac[, measurevar] - datac$ci) else 0\n\n\t\treturn(datac)\n\t}\n\t"
                       "DET <- summarySE(DET, measurevar=\"Y\", groupvars=c(\"%2\", \"X\"))\n\t"
                       "IET <- summarySE(IET, measurevar=\"Y\", groupvars=c(\"%2\", \"X\"))\n\t"
                       "CMC <- summarySE(CMC, measurevar=\"Y\", groupvars=c(\"%2\", \"X\"))\n\t"
                       "ERR <- summarySE(ERR, measurevar=\"X\", groupvars=c(\"Error\", \"%2\", \"Y\"))\n\t"
                       "TF <- summarySE(TF, measurevar=\"Y\", groupvars=c(\"%2\", \"X\"))\n\t"
                       "FT <- summarySE(FT, measurevar=\"Y\", groupvars=c(\"%2\", \"X\"))\n\t"
                       "CT <- summarySE(CT, measurevar=\"Y\", groupvars=c(\"%2\", \"X\"))\n}\n\n"
                       "# Code to format FAR values\n"
                       "far_names <- list('0.001'=\"FAR = 0.1%\", '0.01'=\"FAR = 1%\")\n"
                       "far_labeller <- function(variable,value) { return(far_names[as.character(value)]) }\n"
                       "\n").arg((major.smooth || minor.smooth) ? "TRUE" : "FALSE",
                                 major.size > 1 ? major.header : (minor.header.isEmpty() ? major.header : minor.header),
                                 QString::number(confidence))));

        // Open output device
        file.write(qPrintable(QString("\n"
                                      "# Open output device\n"
                                      "%1(\"%2.%1\"%3)\n").arg(suffix, basename, suffix != "pdf" ? ", width=800, height=800" : "")));

        // Write figures
        file.write("\n"
                   "# Write figures\n");
    }

    void plotMetadata(bool csv)
    {
        file.write(qPrintable(QString("# Code to format TAR@FAR table\n"
                                      "algs <- unique(%4)\n"
                                      "algs <- algs[!duplicated(algs)]\n"
                                      "mat <- matrix(%1,nrow=6,ncol=length(algs),byrow=FALSE)\n"
                                      "colnames(mat) <- algs \n"
                                      "rownames(mat) <- c(\"FAR = 1e-06\", \"FAR = 1e-05\", \"FAR = 1e-04\", \"FAR = 1e-03\", \"FAR = 1e-02\", \"FAR = 1e-01\")\n"
                                      "TFtable <- as.table(mat)\n"
                                      "\n"
                                      "# Code to format FAR@TAR table\n"
                                      "mat <- matrix(%2,nrow=6,ncol=length(algs),byrow=FALSE)\n"
                                      "colnames(mat) <- algs \n"
                                      "rownames(mat) <- c(\"TAR = 0.40\", \"TAR = 0.50\", \"TAR = 0.65\", \"TAR = 0.75\", \"TAR = 0.85\", \"TAR = 0.95\")\n"
                                      "FTtable <- as.table(mat)\n"
                                      "\n"
                                      "# Code to format CMC Table\n"
                                      "mat <- matrix(%3,nrow=6,ncol=length(algs),byrow=FALSE)\n"
                                      "colnames(mat) <- algs \n"
                                      "rownames(mat) <- c(\"Rank 1\", \"Rank 5\", \"Rank 10\", \"Rank 20\", \"Rank 50\", \"Rank 100\")\n"
                                      "CMCtable <- as.table(mat)\n"
                                      "\n"
                                      "# Code to format Template Size Table\n"
                                      "if (nrow(TS) != 0) {\n\t"
                                      "mat <- matrix(TS$Y,nrow=1,ncol=length(algs),byrow=FALSE)\n\t"
                                      "colnames(mat) <- algs\n\t"
                                      "rownames(mat) <- c(\"Template Size (bytes):\")\n\t"
                                      "TStable <- as.table(mat)\n}"
                                      "\n").arg((major.smooth || minor.smooth) && confidence != 0 ? "paste(as.character(round(TF$Y, 3)), round(TF$ci, 3), sep=\"\\u00b1\")" : "TF$Y",
                                                (major.smooth || minor.smooth) && confidence != 0 ? "paste(as.character(round(FT$Y, 3)), round(FT$ci, 3), sep=\"\\u00b1\")" : "FT$Y",
                                                (major.smooth || minor.smooth) && confidence != 0 ? "paste(as.character(round(CT$Y, 3)), round(CT$ci, 3), sep=\"\\u00b1\")" : "CT$Y",
                                                (major.size > 1 && minor.size > 1) && !(major.smooth || minor.smooth) ? QString("paste(TF$%1, TF$%2, sep=\"_\")").arg(major.header, minor.header)
                                                                                                                      : QString("TF$%1").arg(major.size > 1 ? major.header : (minor.header.isEmpty() ? major.header : minor.header)))));

        file.write("\n# Write metadata table\n");
        QString textplot = "MT <- as.data.frame(Metadata[c(1,2,3,4,5),])\n"
                           "par(mfrow=c(4,1))\n"
                           "plot.new()\n"
                           "print(title(paste(\"%1 - %2\",date(),sep=\"\\n\")))\n"
                           "mat <- matrix(MT$X[c(1,2)],ncol=2)\n"
                           "colnames(mat) <- c(\"Gallery\", \"Probe\")\n"
                           "imageTable <- as.table(mat)\n"
                           "print(textplot(imageTable,show.rownames=FALSE))\n"
                           "print(title(\"Images\"))\n"
                           "mat <- matrix(MT$X[c(3,4,5)],ncol=3)\n"
                           "colnames(mat) <- c(\"Genuine\", \"Impostor\", \"Ignored\")\n"
                           "matchTable <- as.table(mat)\n"
                           "print(textplot(matchTable,show.rownames=FALSE))\n"
                           "print(title(\"Matches\"))\n"
                           "plot.new()\n"
                           "print(title(\"Gallery * Probe = Genuine + Impostor + Ignored\"))\n";
        file.write(qPrintable(textplot.arg(PRODUCT_NAME, PRODUCT_VERSION)));

        if (csv)
            textplot = QString("write.csv(TFtable,file=\"%1_TF.csv\")\n"
                       "write.csv(FTtable,file=\"%1_FT.csv\")\n"
                       "write.csv(CMCtable,file=\"%1_CMC.csv\")\n\n").arg(basename);
        else
            textplot = "plot.new()\n"
                       "print(textplot(TFtable))\n"
                       "print(title(\"Table of True Accept Rates at various False Accept Rates\"))\n"
                       "print(textplot(FTtable))\n"
                       "print(title(\"Table  of False Accept Rates at various True Accept Rates\"))\n"
                       "print(textplot(CMCtable))\n"
                       "print(title(\"Table of retrieval rate at various ranks\"))\n"
                       "if (nrow(TS) != 0) {\n\t"
                       "print(textplot(TStable, cex=1.15))\n\t"
                       "print(title(\"Template Size by Algorithm\"))\n}\n\n";
        file.write(qPrintable(textplot));
    }

    void qplot(QString geom, QString data, bool flipY, File opts)
    {
        file.write(qPrintable(QString("qplot(X, %1, data=%2, geom=\"%3\", main=\"%4\"").arg(flipY ? "1-Y" : "Y", data, geom, opts.get<QString>("title", "")) +
                              (opts.contains("size") ? QString(", size=I(%1)").arg(opts.get<QString>("size")) : QString()) +
                              (major.size > 1 ? QString(", colour=factor(%1)").arg(major.header) : QString()) +
                              (minor.size > 1 ? QString(", linetype=factor(%1)").arg(minor.header) : QString()) +
                              (QString(", xlab=\"%1\", ylab=\"%2\") + theme_minimal()").arg(opts.get<QString>("xLab"), opts.get<QString>("yLab"))) +
                              ((major.smooth || minor.smooth) && confidence != 0 && data != "CMC" ? QString(" + geom_errorbar(data=%1[seq(1, NROW(%1), by = 29),], aes(x=X, ymin=%2), width=0.1, alpha=I(1/2))").arg(data, flipY ? "(1-lower), ymax=(1-upper)" : "lower, ymax=upper") : QString()) +
                              (major.size > 1 ? getScale("colour", major.header, major.size) : QString()) +
                              (minor.size > 1 ? QString(" + scale_linetype_discrete(\"%1\")").arg(minor.header) : QString()) +
                              (opts.getBool("xLog") ? QString(" + scale_x_log10(labels=%1) + annotation_logticks(sides=\"b\")").arg(data == "CMC" ? "c(1,5,10,50,100), breaks=c(1,5,10,50,100)" : "trans_format(\"log10\", math_format())")
                                                    : QString(" + scale_x_continuous(labels=%1, breaks=pretty_breaks(n=%2))").arg(data == "CMC" ? "waiver()" : "percent", opts.get<QString>("xTicks", "10"))) +
                              (opts.getBool("yLog") ? " + scale_y_log10(labels=trans_format(\"log10\", math_format())) + annotation_logticks(sides=\"l\")"
                                                    : QString(" + scale_y_continuous(labels=percent, breaks=pretty_breaks(n=%1))").arg(opts.get<QString>("yTicks", "10"))) +
                              (opts.contains("xLimits") ? QString(" + xlim%1").arg(QtUtils::toString(opts.get<QPointF>("xLimits", QPointF()))) : QString()) +
                              (opts.contains("yLimits") ? QString(" + ylim%1").arg(QtUtils::toString(opts.get<QPointF>("yLimits", QPointF()))) : QString()) +
                              QString(" + theme(legend.title = element_text(size = %1), legend.text = element_text(size = %1), plot.title = element_text(size = %1), axis.text = element_text(size = %1), axis.title.x = element_text(size = %1), axis.title.y = element_text(size = %1),").arg(QString::number(opts.get<float>("textSize",12))) +
                              QString(" legend.position=%1, legend.background = element_rect(fill = 'white'), panel.grid.major = element_line(colour = \"gray\"), panel.grid.minor = element_line(colour = \"gray\", linetype = \"dashed\"))").arg(opts.contains("legendPosition") ? "c"+QtUtils::toString(opts.get<QPointF>("legendPosition")) : "'bottom'") +
                              QString(" + guides(col=guide_legend(ncol=%1))\n\n").arg(ncol)));
    }

    bool finalize(bool show = false)
    {
        file.write("dev.off()\n");
        if (suffix != "pdf") file.write(qPrintable(QString("unlink(\"%1.%2\")").arg(basename, suffix)));
        file.close();

        bool success = QtUtils::runRScript(file.fileName());
        if (success && show) QtUtils::showFile(basename+"."+suffix);
        return success;
    }
};

// Does not work if dataset folder starts with a number
bool Plot(const QStringList &files, const File &destination, bool show)
{
    qDebug("Plotting %d file(s) to %s", files.size(), qPrintable(destination));

    RPlot p(files, destination);

    // Use a br::file for simple storage of plot options
    QMap<QString,File> optMap;
    optMap.insert("rocOptions", File(QString("[xLab=False Accept Rate,yLab=True Accept Rate,xLog=true,yLog=false]")));
    optMap.insert("detOptions", File(QString("[xLab=False Accept Rate,yLab=False Reject Rate,xLog=true,yLog=true]")));
    optMap.insert("ietOptions", File(QString("[xLab=False Positive Identification Rate (FPIR),yLab=False Negative Identification Rate (FNIR),xLog=true,yLog=true]")));
    optMap.insert("cmcOptions", File(QString("[xLab=Rank,yLab=Retrieval Rate,xLog=true,yLog=false,size=1]")));

    foreach (const QString &key, optMap.keys()) {
        const QStringList options = destination.get<QStringList>(key, QStringList());
        foreach (const QString &option, options) {
            QStringList words = QtUtils::parse(option, '=');
            QtUtils::checkArgsSize(words[0], words, 1, 2);
            optMap[key].set(words[0], words[1]);
        }
    }

    // optional plot metadata and accuracy tables
    if (destination.getBool("metadata", true))
        p.plotMetadata(destination.getBool("csv", false));
    p.qplot("line", "DET", true, optMap["rocOptions"]);
    p.qplot("line", "DET", false, optMap["detOptions"]);
    p.qplot("line", "IET", false, optMap["ietOptions"]);
    p.qplot("line", "CMC", false, optMap["cmcOptions"]);

    p.file.write(qPrintable(QString("qplot(X, data=SD, geom=\"histogram\", fill=Y, position=\"identity\", alpha=I(1/2)") +
                            QString(", xlab=\"Score\", ylab=\"Frequency\"") +
                            QString(") + scale_fill_manual(\"Ground Truth\", values=c(\"blue\", \"red\")) + theme_minimal() + scale_x_continuous(minor_breaks=NULL) + scale_y_continuous(minor_breaks=NULL) + theme(axis.text.y=element_blank(), axis.ticks=element_blank(), axis.text.x=element_text(angle=-90, hjust=0))") +
                            (p.major.size > 1 ? (p.minor.size > 1 ? QString(" + facet_grid(%2 ~ %1, scales=\"free\")").arg((p.flip ? p.major.header : p.minor.header), (p.flip ? p.minor.header : p.major.header)) : QString(" + facet_wrap(~ %1, scales = \"free\")").arg(p.major.header)) : QString()) +
                            QString(" + theme(aspect.ratio=1)\n\n")));

    p.file.write(qPrintable(QString("qplot(factor(%1)%2, data=BC, %3").arg(p.major.smooth ? (p.minor.header.isEmpty() ? "Algorithm" : p.minor.header) : p.major.header, (p.major.smooth || p.minor.smooth) ? ", Y" : "", (p.major.smooth || p.minor.smooth) ? "geom=\"boxplot\"" : "geom=\"bar\", position=\"dodge\", weight=Y") +
                            (p.major.size > 1 ? QString(", fill=factor(%1)").arg(p.major.header) : QString()) +
                            QString(", xlab=\"False Accept Rate\", ylab=\"True Accept Rate\") + theme_minimal()") +
                            (p.major.size > 1 ? getScale("fill", p.major.header, p.major.size) : QString()) +
                            (p.minor.size > 1 ? QString(" + facet_grid(%2 ~ X)").arg(p.minor.header) : QString(" + facet_grid(. ~ X, labeller=far_labeller)")) +
                            QString(" + scale_y_continuous(labels=percent) + theme(legend.position=\"none\", axis.text.x=element_text(angle=-90, hjust=0))%1").arg((p.major.smooth || p.minor.smooth) ? "" : " + geom_text(data=BC, aes(label=Y, y=0.05))") + "\n\n"));

    p.file.write(qPrintable(QString("qplot(X, Y, data=ERR, geom=\"line\", linetype=Error") +
                            ((p.flip ? p.major.size : p.minor.size) > 1 ? QString(", colour=factor(%1)").arg(p.flip ? p.major.header : p.minor.header) : QString()) +
                            QString(", xlab=\"Score\", ylab=\"Error Rate\") + theme_minimal()") +
                            ((p.flip ? p.major.size : p.minor.size) > 1 ? getScale("colour", p.flip ? p.major.header : p.minor.header, p.flip ? p.major.size : p.minor.size) : QString()) +
                            QString(" + scale_y_log10(labels=percent) + annotation_logticks(sides=\"l\")") +
                            ((p.flip ? p.minor.size : p.major.size) > 1 ? QString(" + facet_wrap(~ %1, scales=\"free_x\")").arg(p.flip ? p.minor.header : p.major.header) : QString()) +
                            QString(" + theme(aspect.ratio=1)\n\n")));

    p.file.write(qPrintable(QString("if (nrow(IM) != 0) {\n\tlibrary(jpeg)\n\tlibrary(png)\n\tlibrary(grid)\n\t") +
                            QString("multiplot <- function(..., plotlist=NULL, cols) {\n\t") +
                            QString("\trequire(grid)\n\n\t\t# Make a list from the ... arguments and plotlist\n\t\tplots <- c(list(...), plotlist)\n") + 
                            QString("\t\tnumPlots = length(plots)\n\n\t\t# Make the panel\n\t\tplotCols = cols\n\t\tplotRows = ceiling(numPlots/plotCols)\n\n") +
                            QString("\t\t# Set up the page\n\t\tgrid.newpage()\n\t\tpushViewport(viewport(layout = grid.layout(plotRows, plotCols)))\n\t\tvplayout <- function(x, y)\n\t\t\tviewport(layout.pos.row = x, layout.pos.col = y)\n\n") +
                            QString("\t\t# Make each plot, in the correct location\n\t\tfor (i in 1:numPlots) {\n\t\t\tcurRow = ceiling(i/plotCols)\n\t\t\tcurCol = (i-1) %% plotCols + 1\n\t\t\tprint(plots[[i]], vp = vplayout(curRow, curCol))\n\t\t}\n\t}\n\n")));
    
    p.file.write(qPrintable(QString("\t# Print impostor matches above the EER\n\tfor (i in 1:nrow(IM)) {\n\t\tscore <- IM[i,1]\n\t\tfiles <- IM[i,2]\n\t\talg <- IM[i,3]\n\t\tfiles <- unlist(strsplit(files, \"[:]\"))\n\n\t\text1 <- unlist(strsplit(files[2], \"[.]\"))[2]\n\t\text2 <- unlist(strsplit(files[4], \"[.]\"))[2]\n\t\t") +
                            QString("if (ext1 == \"jpg\" || ext1 == \"JPEG\" || ext1 == \"jpeg\" || ext1 == \"JPG\") {\n\t\t\timg1 <- readJPEG(files[2])\n\t\t} else if (ext1 == \"PNG\" || ext1 == \"png\") {\n\t\t\timg1 <- readPNG(files[2])\n\t\t} else if (ext1 == \"TIFF\" || ext1 == \"tiff\" || ext1 == \"TIF\" || ext1 == \"tif\") {\n\t\t\timg1 <- readTIFF(files[2])\n\t\t} else {\n\t\t\tnext\n\t\t}\n\t\tif (ext2 == \"jpg\" || ext2 == \"JPEG\" || ext2 == \"jpeg\" || ext2 == \"JPG\") {\n\t\t\timg2 <- readJPEG(files[4])\n\t\t} ") +
                            QString("else if (ext2 == \"PNG\" || ext2 == \"png\") {\n\t\t\timg2 <- readPNG(files[4])\n\t\t} else if (ext2 == \"TIFF\" || ext2 == \"tiff\" || ext2 == \"TIF\" || ext2 == \"tif\") {\n\t\t\timg2 <- readTIFF(files[4])\n\t\t} else {\n\t\t\tnext\n\t\t}") +
                            QString("\n\t\tname1 <- files[1]\n\t\tname2 <- files[3]\n\n\t\tg1 <- rasterGrob(img1, interpolate=TRUE)\n\t\tg2 <- rasterGrob(img2, interpolate=TRUE)\n\n\t\t") +
                            QString("plot1 <- qplot(1:10, 1:10, geom=\"blank\") + annotation_custom(g1, xmin=-Inf, xmax=Inf, ymin=-Inf, ymax=Inf) + theme(axis.line=element_blank(), axis.text.x=element_blank(), axis.text.y=element_blank(), axis.ticks=element_blank(), panel.background=element_blank()) + labs(title=alg) + ylab(unlist(strsplit(files[2], \"[/]\"))[length(unlist(strsplit(files[2], \"[/]\")))]) + xlab(name1)\n\t\t") +
                            QString("plot2 <- qplot(1:10, 1:10, geom=\"blank\") + annotation_custom(g2, xmin=-Inf, xmax=Inf, ymin=-Inf, ymax=Inf) + theme(axis.line=element_blank(), axis.text.x=element_blank(), axis.text.y=element_blank(), axis.ticks=element_blank(), panel.background=element_blank()) + labs(title=paste(\"Impostor score =\", score)) + ylab(unlist(strsplit(files[4], \"[/]\"))[length(unlist(strsplit(files[4], \"[/]\")))]) + xlab(name2)\n\n\t\t") +
                            QString("multiplot(plot1, plot2, cols=2)\n\t}")));

    p.file.write(qPrintable(QString("\n\n\t# Print genuine matches below the EER\n\tfor (i in 1:nrow(GM)) {\n\t\tscore <- GM[i,1]\n\t\tfiles <- GM[i,2]\n\t\talg <- GM[i,3]\n\t\tfiles <- unlist(strsplit(files, \"[:]\"))\n\n\t\text1 <- unlist(strsplit(files[2], \"[.]\"))[2]\n\t\text2 <- unlist(strsplit(files[4], \"[.]\"))[2]\n\t\t") +
                            QString("if (ext1 == \"jpg\" || ext1 == \"JPEG\" || ext1 == \"jpeg\" || ext1 == \"JPG\") {\n\t\t\timg1 <- readJPEG(files[2])\n\t\t} else if (ext1 == \"PNG\" || ext1 == \"png\") {\n\t\t\timg1 <- readPNG(files[2])\n\t\t} else if (ext1 == \"TIFF\" || ext1 == \"tiff\" || ext1 == \"TIF\" || ext1 == \"tif\") {\n\t\t\timg1 <- readTIFF(files[2])\n\t\t} else {\n\t\t\tnext\n\t\t}\n\t\tif (ext2 == \"jpg\" || ext2 == \"JPEG\" || ext2 == \"jpeg\" || ext2 == \"JPG\") {\n\t\t\timg2 <- readJPEG(files[4])\n\t\t} ") +
                            QString("else if (ext2 == \"PNG\" || ext2 == \"png\") {\n\t\t\timg2 <- readPNG(files[4])\n\t\t} else if (ext2 == \"TIFF\" || ext2 == \"tiff\" || ext2 == \"TIF\" || ext2 == \"tif\") {\n\t\t\timg2 <- readTIFF(files[4])\n\t\t} else {\n\t\t\tnext\n\t\t}") +
                            QString("\n\t\tname1 <- files[1]\n\t\tname2 <- files[3]\n\n\t\tg1 <- rasterGrob(img1, interpolate=TRUE)\n\t\tg2 <- rasterGrob(img2, interpolate=TRUE)\n\n\t\t") +
                            QString("plot1 <- qplot(1:10, 1:10, geom=\"blank\") + annotation_custom(g1, xmin=-Inf, xmax=Inf, ymin=-Inf, ymax=Inf) + theme(axis.line=element_blank(), axis.text.x=element_blank(), axis.text.y=element_blank(), axis.ticks=element_blank(), panel.background=element_blank()) + labs(title=alg) + ylab(unlist(strsplit(files[2], \"[/]\"))[length(unlist(strsplit(files[2], \"[/]\")))]) + xlab(name1)\n\t\t") +
                            QString("plot2 <- qplot(1:10, 1:10, geom=\"blank\") + annotation_custom(g2, xmin=-Inf, xmax=Inf, ymin=-Inf, ymax=Inf) + theme(axis.line=element_blank(), axis.text.x=element_blank(), axis.text.y=element_blank(), axis.ticks=element_blank(), panel.background=element_blank()) + labs(title=paste(\"Genuine score =\", score)) + ylab(unlist(strsplit(files[4], \"[/]\"))[length(unlist(strsplit(files[4], \"[/]\")))]) + xlab(name2)\n\n\t\t") +
                            QString("multiplot(plot1, plot2, cols=2)\n\t}\n}\n\n")));

    return p.finalize(show);
}

//Check if only one ROC point is in the file
bool fileHasSinglePoint(const QString &evalFile) {
    QFile file(evalFile);
    bool success = file.open(QFile::ReadOnly);
    if (!success) qFatal("Failed to open %s for reading.", qPrintable(evalFile));
    QStringList lines = QString(file.readAll()).split("\n");
    file.close();

    int rocCnt = 0;
    foreach (const QString &line, lines) {
        if (line.contains("DiscreteROC")) {
            rocCnt++;
        }
        if (rocCnt > 1)
            return false;
    }

    return true;
}

//Check all files to see if any single file has only have one ROC point
bool filesHaveSinglePoint(const QStringList &files) {
    foreach (const File &file, files)
        if (fileHasSinglePoint(file))
            return true;
    return false;
}

// Properly

bool PlotDetection(const QStringList &files, const File &destination, bool show)
{
    qDebug("Plotting %d detection file(s) to %s", files.size(), qPrintable(destination));
    RPlot p(files, destination, false);

    // Use a br::file for simple storage of plot options
    QMap<QString,File> optMap;
    optMap.insert("rocOptions", File(QString("[xLab=False Accepts Per Image,yLab=True Accept Rate,xLog=true,yLog=false]")));
    optMap.insert("prOptions", File(QString("[xLab=False Accept Rate,yLab=False Reject Rate,xLog=true,yLog=true]")));

    foreach (const QString &key, optMap.keys()) {
        const QStringList options = destination.get<QStringList>(key, QStringList());
        foreach (const QString &option, options) {
            QStringList words = QtUtils::parse(option, '=');
            QtUtils::checkArgsSize(words[0], words, 1, 2);
            optMap[key].set(words[0], words[1]);
        }
    }

    p.file.write("# Split data into individual plots\n"
                 "plot_index = which(names(data)==\"Plot\")\n"
                 "DiscreteROC <- data[grep(\"DiscreteROC\",data$Plot),-c(1)]\n"
                 "ContinuousROC <- data[grep(\"ContinuousROC\",data$Plot),-c(1)]\n"
                 "DiscretePR <- data[grep(\"DiscretePR\",data$Plot),-c(1)]\n"
                 "ContinuousPR <- data[grep(\"ContinuousPR\",data$Plot),-c(1)]\n"
                 "Overlap <- data[grep(\"Overlap\",data$Plot),-c(1)]\n"
                 "AverageOverlap <- data[grep(\"AverageOverlap\",data$Plot),-c(1)]\n"
                 "rm(data)\n"
                 "\n");

    QString plotType("line");
    if (filesHaveSinglePoint(files))
        plotType = QString("point");

    foreach (const QString &type, QStringList() << "Discrete" << "Continuous") {
        optMap["rocOptions"].set("title", type);
        p.qplot(plotType, type + "ROC", false, optMap["rocOptions"]);
    }

    foreach (const QString &type, QStringList() << "Discrete" << "Continuous") {
        optMap["prOptions"].set("title", type);
        p.qplot(plotType, type + "PR", false, optMap["prOptions"]);
    }

    p.file.write(qPrintable(QString("qplot(X, data=Overlap, geom=\"histogram\", position=\"identity\", xlab=\"Overlap\", ylab=\"Frequency\")") +
                            QString(" + theme_minimal() + scale_x_continuous(minor_breaks=NULL) + scale_y_continuous(minor_breaks=NULL) + theme(axis.text.y=element_blank(), axis.ticks=element_blank(), axis.text.x=element_text(angle=-90, hjust=0))") +
                            (p.major.size > 1 ? (p.minor.size > 1 ? QString(" + facet_grid(%2 ~ %1, scales=\"free\")").arg(p.minor.header, p.major.header) : QString(" + facet_wrap(~ %1, scales = \"free\")").arg(p.major.header)) : QString()) +
                            QString(" + theme(aspect.ratio=1, legend.position=\"bottom\")\n\n")));

    p.file.write(qPrintable(QString("ggplot(AverageOverlap, aes(x=%1, y=%2, label=round(X,3)), main=\"Average Overlap\") + geom_text() + theme_minimal()").arg(p.minor.size > 1 ? p.minor.header : "'X'", p.major.size > 1 ? p.major.header : "'Y'") +
                            QString("%1%2\n\n").arg(p.minor.size > 1 ? "" : " + xlab(NULL)", p.major.size > 1 ? "" : " + ylab(NULL)")));

    p.file.write(qPrintable(QString("ggplot(AverageOverlap, aes(x=%1, y=%2, fill=X)) + geom_tile() + scale_fill_continuous(\"Average Overlap\") + theme_minimal()").arg(p.minor.size > 1 ? p.minor.header : "'X'", p.major.size > 1 ? p.major.header : "'Y'") +
                            QString("%1%2\n\n").arg(p.minor.size > 1 ? "" : " + xlab(NULL)", p.major.size > 1 ? "" : " + ylab(NULL)")));

    return p.finalize(show);
}

bool PlotLandmarking(const QStringList &files, const File &destination, bool show)
{
    qDebug("Plotting %d landmarking file(s) to %s", files.size(), qPrintable(destination));
    RPlot p(files, destination, false);

    p.file.write(qPrintable(QString("# Split data into individual plots\n"
                                    "plot_index = which(names(data)==\"Plot\")\n"
                                    "Box <- data[grep(\"Box\",data$Plot),-c(1)]\n"
                                    "Box$X <- factor(Box$X, levels = Box$X, ordered = TRUE)\n"
                                    "Sample <- data[grep(\"Sample\",data$Plot),-c(1)]\n"
                                    "Sample$X <- as.character(Sample$X)\n"
                                    "EXT <- data[grep(\"EXT\",data$Plot),-c(1)]\n"
                                    "EXT$X <- as.character(EXT$X)\n"
                                    "EXP <- data[grep(\"EXP\",data$Plot),-c(1)]\n"
                                    "EXP$X <- as.character(EXP$X)\n"
                                    "NormLength <- data[grep(\"NormLength\",data$Plot),-c(1)]\n"
                                    "rm(data)\n"
                                    "\n")));

    p.file.write(qPrintable(QString("summarySE <- function(data=NULL, measurevar, groupvars=NULL, na.rm=FALSE, conf.interval=.95, .drop=TRUE) {\n\t"
                                    "require(plyr)\n\n\tlength2 <- function (x, na.rm=FALSE) {\n\t\tif (na.rm) sum(!is.na(x))\n\t\telse       length(x)"
                                    "\n\t}\n\n\tdatac <- ddply(data, groupvars, .drop=.drop, .fun = function(xx, col) {\n\t\t"
                                    "c(N=length2(xx[[col]], na.rm=na.rm), mean=mean(xx[[col]], na.rm=na.rm), sd=sd(xx[[col]], na.rm=na.rm))\n\t\t},"
                                    "\n\t\tmeasurevar\n\t)\n\n\tdatac <- rename(datac, c(\"mean\" = measurevar))\n\tdatac$se <- datac$sd / sqrt(datac$N)"
                                    "\n\tciMult <- qt(conf.interval/2 + .5, datac$N-1)\n\tdatac$ci <- datac$se * ciMult\n\n\treturn(datac)\n}\n")));


    p.file.write(qPrintable(QString("\nreadData <- function(data) {\n\texamples <- list()\n"
                                    "\tfor (i in 1:nrow(data)) {\n"
                                    "\t\tpath <- data[i,1]\n"
                                    "\t\tvalue <- data[i,2]\n"
                                    "\t\tfile <- unlist(strsplit(path, \"[.]\"))[1]\n"
                                    "\t\text <- unlist(strsplit(path, \"[.]\"))[2]\n"
                                    "\t\tif (ext == \"jpg\" || ext == \"JPEG\" || ext == \"jpeg\" || ext == \"JPG\") {\n"
                                    "\t\t\timg <- readJPEG(path)\n"
                                    "\t\t} else if (ext == \"PNG\" || ext == \"png\") {\n"
                                    "\t\t\timg <- readPNG(path)\n"
                                    "\t\t} else if (ext == \"TIFF\" || ext == \"tiff\" || ext == \"TIF\" || ext == \"tif\") { \n"
                                    "\t\t\timg <- readTIFF(path)\n"
                                    "}else {\n"
                                    "\t\t\tnext\n"
                                    "\t\t}\n"
                                    "\t\texample <- list(file = file, value = value, image = img)\n"
                                    "\t\texamples[[i]] <- example\n"
                                    "\t}\n"
                                    "\treturn(examples)\n"
                                    "}\n")));

    p.file.write(qPrintable(QString("\nlibrary(jpeg)\n"
                                    "library(png)\n"
                                    "library(grid)\n"
                                    "multiplot <- function(..., plotlist=NULL, cols) {\n"
                                    "\trequire(grid)\n"
                                    "\t# Make a list from the ... arguments and plotlist\n"
                                    "\tplots <- c(list(...), plotlist)\n"
                                    "\tnumPlots = length(plots)\n"
                                    "\t# Make the panel\n"
                                    "\tplotCols = cols\n"
                                    "\tplotRows = ceiling(numPlots/plotCols)\n"
                                    "\t# Set up the page\n"
                                    "\tgrid.newpage()\n"
                                    "\tpushViewport(viewport(layout = grid.layout(plotRows, plotCols)))\n"
                                    "\tvplayout <- function(x, y)\n"
                                    "\tviewport(layout.pos.row = x, layout.pos.col = y)\n"
                                    "\t# Make each plot, in the correct location\n"
                                    "\tfor (i in 1:numPlots) {\n"
                                    "\t\tcurRow = ceiling(i/plotCols)\n"
                                    "\t\tcurCol = (i-1) %% plotCols + 1\n"
                                    "\t\tprint(plots[[i]], vp = vplayout(curRow, curCol))\n"
                                    "\t}\n"
                                    "}\n")));

    p.file.write(qPrintable(QString("\nplotImage <- function(image, title=NULL, label=NULL) { \n"
                                    "\tp <- qplot(1:10, 1:10, geom=\"blank\") + annotation_custom(rasterGrob(image$image), xmin=-Inf, xmax=Inf, ymin=-Inf, ymax=Inf) + theme(axis.line=element_blank(), axis.title.y=element_blank(), axis.text.x=element_blank(), axis.text.y=element_blank(), line=element_blank(), axis.ticks=element_blank(), panel.background=element_blank()) + labs(title=title) + xlab(label)\n"
                                    "\treturn(p)"
                                    "}\n")));

    p.file.write(qPrintable(QString("\nsample <- readData(Sample) \n"
                                    "rows <- sample[[1]]$value\n"
                                    "algs <- unique(Box$%1)\n"
                                    "algs <- algs[!duplicated(algs)]\n"
                                    "print(plotImage(sample[[1]],\"Sample Landmarks\",sprintf(\"Total Landmarks: %s\",sample[[1]]$value))) \n"
                                    "if (nrow(EXT) != 0 && nrow(EXP)) {\n"
                                    "\tfor (j in 1:length(algs)) {\n"
                                    "\ttruthSample <- readData(EXT[EXT$. == algs[[j]],])\n"
                                    "\tpredictedSample <- readData(EXP[EXP$. == algs[[j]],])\n"
                                    "\t\tfor (i in 1:length(predictedSample)) {\n"
                                    "\t\t\tmultiplot(plotImage(predictedSample[[i]],sprintf(\"%s\\nPredicted Landmarks\",algs[[j]]),sprintf(\"Average Landmark Error: %.3f\",predictedSample[[i]]$value)),plotImage(truthSample[[i]],\"Ground Truth\\nLandmarks\",\"\"),cols=2)\n"
                                    "\t\t}\n"
                                    "\t}\n"
                                    "}\n").arg(p.major.size > 1 ? p.major.header : (p.minor.header.isEmpty() ? p.major.header : p.minor.header))));

    p.file.write(qPrintable(QString("\n"
                 "# Code to format error table\n"
                 "StatBox <- summarySE(Box, measurevar=\"Y\", groupvars=c(\"%1\",\"X\"))\n"
                 "OverallStatBox <- summarySE(Box, measurevar=\"Y\", groupvars=c(\"%1\"))\n"
                 "mat <- matrix(paste(as.character(round(StatBox$Y, 3)), round(StatBox$ci, 3), sep=\" \\u00b1 \"),nrow=rows,ncol=length(algs),byrow=FALSE)\n"
                 "mat <- rbind(mat, paste(as.character(round(OverallStatBox$Y, 3)), round(OverallStatBox$ci, 3), sep=\" \\u00b1 \"))\n"
                 "mat <- rbind(mat, as.character(round(NormLength$Y, 3)))\n"
                 "colnames(mat) <- algs\n"
                 "rownames(mat) <- c(seq(0,rows-1),\"Aggregate\",\"Average IPD\")\n"
                 "ETable <- as.table(mat)\n").arg(p.major.size > 1 ? p.major.header : (p.minor.header.isEmpty() ? p.major.header : p.minor.header))));

    p.file.write(qPrintable(QString("\n"
                       "print(textplot(ETable))\n"
                       "print(title(\"Landmarking Error Rates\"))\n")));

    p.file.write(qPrintable(QString("ggplot(Box, aes(Y,%1%2))").arg(p.major.size > 1 ? QString(", colour=%1").arg(p.major.header) : QString(),
                                                                    p.minor.size > 1 ? QString(", linetype=%1").arg(p.minor.header) : QString()) +
                            QString(" + annotation_logticks(sides=\"b\") + stat_ecdf() + scale_x_log10(\"Normalized Error\", breaks=c(0.001,0.01,0.1,1,10)) + scale_y_continuous(\"Cumulative Density\", label=percent) + theme_minimal()\n\n")));

    p.file.write(qPrintable(QString("ggplot(Box, aes(factor(X), Y%1%2))").arg(p.major.size > 1 ? QString(", colour=%1").arg(p.major.header) : QString(), p.minor.size > 1 ? QString(", linetype=%1").arg(p.minor.header) : QString()) +
                            QString("+ annotation_logticks(sides=\"l\") + geom_boxplot(alpha=0.5) + geom_jitter(size=1, alpha=0.5) + scale_x_discrete(\"Landmark\") + scale_y_log10(\"Normalized Error\", breaks=c(0.001,0.01,0.1,1,10)) + theme_minimal()\n\n")));

    p.file.write(qPrintable(QString("ggplot(Box, aes(factor(X), Y%1%2))").arg(p.major.size > 1 ? QString(", colour=%1").arg(p.major.header) : QString(), p.minor.size > 1 ? QString(", linetype=%1").arg(p.minor.header) : QString()) +
                            QString("+ annotation_logticks(sides=\"l\") + geom_violin(alpha=0.5) + scale_x_discrete(\"Landmark\") + scale_y_log10(\"Normalized Error\", breaks=c(0.001,0.01,0.1,1,10))\n\n")));

    return p.finalize(show);
}

bool PlotMetadata(const QStringList &files, const QString &columns, bool show)
{
    qDebug("Plotting %d metadata file(s) for columns %s", files.size(), qPrintable(columns));

    RPlot p(files, "PlotMetadata", false);
    foreach (const QString &column, columns.split(";"))
        p.file.write(qPrintable(QString("qplot(%1, %2, data=data, geom=\"violin\", fill=%1) + coord_flip() + theme_minimal()\nggsave(\"%2.pdf\")\n").arg(p.major.header, column)));
    return p.finalize(show);
}

} // namespace br
