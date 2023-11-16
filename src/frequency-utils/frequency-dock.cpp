#include "frequency-dock.h"

QDockWidget *myDockWidget = nullptr;
QTextBrowser *textBrowser = nullptr;
QPushButton *toggleButton = nullptr;

enum PageType { PUBLIC_PAGE, PRIVATE_PAGE };

PageType currentPage = PUBLIC_PAGE;

void updateDockContent()
{
	if (textBrowser) {
		switch (currentPage) {
		case PUBLIC_PAGE:
			textBrowser->setPlainText("This is public page");
			break;
		case PRIVATE_PAGE:
			textBrowser->setPlainText("This is private page");
			break;
		}
	}
}

void togglePage()
{
	currentPage = (currentPage == PUBLIC_PAGE) ? PRIVATE_PAGE : PUBLIC_PAGE;
	updateDockContent();
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
		textBrowser->setPlainText("This is public page");
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
	}

	updateDockContent();

	return false;
}