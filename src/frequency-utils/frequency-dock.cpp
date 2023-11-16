#include "frequency-dock.h"

QDockWidget *myDockWidget = nullptr;
QTextBrowser *textBrowser = nullptr;
QPushButton *toggleButton = nullptr;
QString base_path = QDir::fromNativeSeparators(obs_frontend_get_current_record_output_path());

enum PageType { PUBLIC_PAGE, PRIVATE_PAGE };

PageType currentPage = PUBLIC_PAGE;

void readFromFile(const QString &filePath)
{
	QFile file(filePath);
	if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		QTextStream in(&file);
		QString content = in.readAll();
		textBrowser->setPlainText(content);
		file.close();
	}
}

void updateDockContent()
{
	if (textBrowser) {
		switch (currentPage) {
		case PUBLIC_PAGE:
			// Read content from a file (you can replace "public_page.txt" with your file path)
            readFromFile(QDir(base_path).filePath("public_page.txt"));
			break;
		case PRIVATE_PAGE:
			// Read content from a different file
            readFromFile(QDir(base_path).filePath("private_page.txt"));
			break;
		}
	}
}

void togglePage()
{
	currentPage = (currentPage == PUBLIC_PAGE) ? PRIVATE_PAGE : PUBLIC_PAGE;
	updateDockContent();
}

void cleanup()
{
	// Perform cleanup operations here, if needed
	delete myDockWidget;
	myDockWidget = nullptr;
	textBrowser = nullptr;
	toggleButton = nullptr;
}

bool buttonClicked(obs_properties_t *props, obs_property_t *property,
		   void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	UNUSED_PARAMETER(data);

	if (!myDockWidget) {
		myDockWidget = new QDockWidget("Statistics");
		obs_frontend_add_dock(myDockWidget);

		textBrowser = new QTextBrowser;
		// Set initial content from file (you can replace "public_page.txt" with your file path)
		readFromFile(base_path + "public_page.txt");
		myDockWidget->setWidget(textBrowser);

		toggleButton = new QPushButton("Public / Private");
		QObject::connect(toggleButton, &QPushButton::clicked,
				 togglePage);

		QVBoxLayout *layout = new QVBoxLayout;
		layout->addWidget(textBrowser);
		layout->addWidget(toggleButton);

		QWidget *centralWidget = new QWidget;
		centralWidget->setLayout(layout);
		myDockWidget->setWidget(centralWidget);

		myDockWidget->show();
	} else {
		// If the QDockWidget is already created, toggle its visibility
		myDockWidget->setVisible(!myDockWidget->isVisible());
		// If the QDockWidget is hidden, delete it to free memory
		if (!myDockWidget->isVisible()) {
			cleanup();
		}
	}

	updateDockContent();

	return false;
}
