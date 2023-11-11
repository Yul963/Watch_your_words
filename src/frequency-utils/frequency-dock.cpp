#include "frequency-dock.h"

QDockWidget *myDockWidget = nullptr;

bool buttonClicked(obs_properties_t *props, obs_property_t *property,
		   void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	UNUSED_PARAMETER(data);

	if (!myDockWidget) {
		// Create the QTextEdit widget
		QTextEdit *textEdit = new QTextEdit("Hello, World!");

		// Create the QDockWidget
		myDockWidget = new QDockWidget("STATISTICS");
		myDockWidget->setWidget(textEdit);

		// Add the QDockWidget to the UI's Docks menu
		obs_frontend_add_dock(myDockWidget);

		// Show the QDockWidget
		myDockWidget->show();
	} else {
		// If the QDockWidget is already created, toggle its visibility
		myDockWidget->setVisible(!myDockWidget->isVisible());
	}

	// Returning false means that the UI properties do not need to be rebuilt
	return false;
}