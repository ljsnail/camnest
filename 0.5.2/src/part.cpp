#include "gatsp.h"
#include "part.h"
///used to manually set the loopNumber
#include <QInputDialog>
///@note:at() can be faster than operator[](), because it never causes a deep copy to occur.

/// The tolerance defines the disstance within it a loop is estimated to be closed
int tolerance=0.01;
/// @todo get from settings lead angle for circle loops
//int leadAngle=180;
bool plasmaMode=true;
///Automatically optize path on part previewing
bool optPath=true;
/** have to create an object lead point , a QGraphicsEllipseItem that is magneted to the endPoint parent
and which position will be used by the G code genrator**/
/**   FIXME  handle the case the part outline is a circle!! */
/** For the outline: We first check if the OutlinePoint intersects with the rectBounds, such point can be trivialy
chosen to be a good candidtae for lean in.In that case we draw a line, its lenght being specified by the user,and we
try with different angular positions till the lead-in point is no longer contained in the RectBound*/

/// @fixme: part ansi/6/14-299 is a total mess in leads!



/**
 * 
 * @param path 
 * @param pointsList 
 * @param partPathsList 
 * @param circlePointsList 
 * @param circlesPathsList 
 * @param file 
 */
Part::Part(const QPainterPath path,QPFWPVector pointsList,QPPVector partPathsList,QPFWPVector circlePointsList,QPPVector circlesPathsList):
        QGraphicsItem(){

    partShape=path;
    qtRect=partShape.boundingRect();
    nbrClosedPath=0;
    nbrCircles=0;
    outlinePos=0;

    partLoops.clear();
    partLoopsOpt.clear();
    ptsVector=pointsList;
    pathsList=partPathsList;
    cirPtsList=circlePointsList;
    cirPathsList=circlesPathsList;

    leadAngle=(settings.value("Leads/angle").toInt());
    leadDist=settings.value("Leads/leadDist").toDouble();
    leadRadius= settings.value("Leads/radius").toDouble();
    optPath=settings.value("Options/optPath").toBool();
    plasmaMode=settings.value("Options/plasmaMode").toBool();
    gLoops.clear();
    gLeadPoints.clear();


    preview=true;
    ///@note although unused in preview-mode we reset the matrix for proper intialisation
    transform.reset();

    ///to keep track of the part Nbr @todo: get this from the scen and use it when deleting parts!!
    ///setData(0,partNumber);

    //setFlag(ItemIsMovable);
    //setFlag(ItemIsSelectable);
    //LoopThread test(this);
    /// For better image rendering use caching
    //setCacheMode(DeviceCoordinateCache);

    ///everything is ready we can start processing the part
    start();
}


///Constructor used to add multiplie part to a sheet by copying the previewed part items
Part::Part(const Part &parentPart):QGraphicsItem(){
    preview=false;
    setFlag(ItemIsMovable);
    setFlag(ItemIsSelectable);
    setFlag(ItemIsFocusable);

    partShape=parentPart.partShape;
    qtRect=parentPart.qtRect;


    ///we start taking transformation into account from here
    transform.reset();

    ///NOw that the leads are cleaned we can copy them to gLoops that will be pocede by GCode
    foreach (Loop* cLoop, parentPart.partLoops){
        gLoops<<cLoop->getEntities();
        ///@todo: check for plasma mode to avoid copying unnessacry things
        gLeadPoints<<cLoop->leadIn;
    }


    //qDebug()<<gLeadPoints;
    /// Any change to preview part will affect the partLoops thus editiing one will edit the others
    partLoops=parentPart.partLoops;
    ///@fixme as getting loops entities causes crashes we're going through gcodeEntities which is "static" by part
    partName=parentPart.partName;
    ///to keep track of the part Nbr
    setData(0,parentPart.data(0));
    ///NOTE:NO need to take the Trans part
}

void Part::start(){

    /// Start by parsing circular loops
    if(cirPtsList.size()!=0){
        filterCircles();
    }

    //Search for continous loops in part.
    if(ptsVector.size()!=0){
        generateLoops(ptsVector);
    }


    if (plasmaMode){
        ///Move the part outline to the List's end it has to be the last to be cut in plasma/laser cutting (if plasmaMode is enabled)

        findOutline();

        //if (partLoops.size()=>1){

        //}
        ///@fixme outlineLead will be created within generateLead after cheking for is outline
        //createOutlineLead();

    ///Now that all the graphical processing is done we "allow" loops to be drawn

    generateLeads();
 }

    ///@todo: add option : optimize auto
    qDebug()<<"Starting optimization";
    ///We try to optimize only if there is at least an outline loop and an inner one
    if ((partLoops.size()>1) && optPath){
        optimizeRoute();

    }
    else  {
    partLoopsOpt=partLoops;
    }

    ///now we can start drawing the loops

    foreach (Loop* cLoop, partLoopsOpt){
        cLoop->setReady(true);
        qDebug()<<"Loops order"<<cLoop->loopNumber;
    }
}


void  Part::filterCircles(){
    /**As we have to return a QList of Qlist we create a temporary one (for now the temp dimension is one
         But when introducing circles lead-in lead-out a circle loop will contain 2 points the center and the lead in**/
    QPFWPVector temp;

    QPointFWithParent currentPoint;
    ///creating a new circleLoop
    Loop *tempLoop=new Loop(this);

    ///@note to avoid crash we append an element to temp and keep changing it
    temp.append(cirPtsList[0]);
    //qDebug()<<"Adding "<<cirPtsList.size()<<" circles";

    ///create a new lop for each encountered circle and set it's number and shape
    for (int pos=0; pos <cirPtsList.size();pos++){
        /**We take a point mark its parent loop umber (for later tsp processing) then
                 appeend it to the endPointslist and add the closedPath counter**/
        currentPoint=cirPtsList.at(pos);
        /// @fixme We need to save the parent loop in the original point for later use in optimize devise a more elegant way to do that
        currentPoint.setLoopNbr(nbrClosedPath);

        //temp[0]=cirPtsList.at(pos);
        temp[0]=currentPoint;

        tempLoop->addPath(cirPathsList.at(pos));
        tempLoop->setStart(currentPoint);
        tempLoop->setEnd(currentPoint);
        tempLoop->setTypeCircle(true);
        tempLoop->setEntities(temp);
        ///@note: have to call setNumber AFTER setEntitites!!
        tempLoop->setNumber(nbrClosedPath);
        partLoops<<tempLoop;

        nbrClosedPath++;
        tempLoop=new Loop(this);

    }
    ///at this step we only have circles
    nbrCircles=nbrClosedPath;
    ///@todo We don't need the path list anymore, free it
    cirPathsList.clear();
    cirPtsList.clear();

}




void Part::generateLoops(QPFWPVector ptsVector){

    Loop *tempLoop=new Loop(this);
    QPointFWithParent currentPoint;

    QPFWPVector pointsVectorNew;

    QPainterPath subLoop;
    bool alreadyChecked=false;
    int oldPos=0,found=1,pos=0;
    ///@fixme:have to change the algorithm I'm working with :sort the points tables and go from one to the one next to as they are QpWP it's all i need
    qDebug() << "Dealing with "<<pathsList.size()<<" Entities represented by "<< ptsVector.size()<<"points";
    while (ptsVector.size()>=2) {
        currentPoint=ptsVector.at(pos);
        //qDebug()<<"Working with"<<currentPoint<< " at "<<pos;
        if (ptsVector.count(currentPoint)==1) {
            oldPos=pos;
            //qDebug()<<"No corresponding point.AlreadyChecked other coord:"<<alreadyChecked;
            if ( alreadyChecked==false ) {
                if (pos%2==0) pos++; else pos--;
                alreadyChecked=true;
            }
            currentPoint=ptsVector.at(pos);
            if (ptsVector.count(currentPoint)==1) {
                //qDebug()<<"Swaped but still no corresponding point.";
                if (oldPos==pos) {
                    if (pos%2==0) pos++; else pos--;
                }
                currentPoint=ptsVector.at(pos);
                shrink (ptsVector,pointsVectorNew,pos,oldPos);
                /** Will be useful later for TSP optimization*/
                pointsVectorNew.first().setLoopNbr(nbrClosedPath);
                pointsVectorNew.last().setLoopNbr(nbrClosedPath);

                /// if this is closed loop we add just one point elsewhere we add the start/end
                subLoop.addPath(pathsList.at(pos/2));

                tempLoop->addPath(pathsList.at(pos/2));
                ///have now to close the path @todo redraw th path correctly
                ///@fixme if arc entity have to move back to arc end!
                //tempLoop->loopShape.closeSubpath();
                ///@note:We have to remove the path for the algo to properly work (not just for memory opt)
                pathsList.remove(pos/2);
                tempLoop->setStart(pointsVectorNew.first());
                tempLoop->setEnd(pointsVectorNew.last());
                ///@todo: have to add a functionthat count and display differently open loops
                tempLoop->setEntities(pointsVectorNew);
                ///@note: have to call setNumber after setEntitites!!
                tempLoop->setNumber(nbrClosedPath);
                /** Now the Loop contains all the necessary parameters to define it:
                                 -start point.
                                 -end point.
                                 -Everypoint between the start and end point.
                                 NOte:
                                 - lead-in / lead-out points will be added further if needed.
                                 */

                partLoops<<tempLoop;

                /// increment only if it's composed of at least one intersection (not neceraly!!!)
                // if (found==1)
                nbrClosedPath ++;
                pointsVectorNew.clear();
                ///@todo:differ painting after optimization...
                /// tempLoop->nowReady();
                tempLoop=new Loop(this);

                subLoop=QPainterPath();
                found=1;
                alreadyChecked=false;
                pos=newPos(ptsVector);
                continue;
            }
            /// After swapping Points Have found that this point do have a correponding point in another entity
            else {
                found++;
                if (found==2) {
                    /// to keep the lines order we have to put currentPoint after oldPoint
                    if (pos%2==0) {oldPos=pos++;} else {oldPos=pos--;}
                    shrink (ptsVector,pointsVectorNew,pos,oldPos);
                    //subLoop.addPath(pathsList.takeAt(pos/2));
                    subLoop.addPath(pathsList.at(pos/2));
                    tempLoop->addPath(pathsList.at(pos/2));
                    pathsList.remove(pos/2);
                    found=1;
                }
                /// get the corresponding point location and store the caller one to avoid infinte loop
                if (ptsVector.indexOf(currentPoint)==pos ) {
                    pos=ptsVector.lastIndexOf(currentPoint);
                }
                else {
                    pos=ptsVector.indexOf(currentPoint);
                }
                /// change to the other the point definfig the line
                if (pos%2==0) pos++; else pos--;
                continue;
            }
        }
        /// Found a corresponding point in another entity
        else {
            found++;
            /// It's time to remove the points and their corresponding entities
            if (found==2) {
                if (pos%2==0) {oldPos=pos++;} else {oldPos=pos--;}
                shrink (ptsVector,pointsVectorNew,pos,oldPos);
                //qDebug () << "taking path at"<<pos<<"path size"<<pathsList.size()<<"ptsList size"<<ptsList.size();
                //subLoop.addPath(pathsList.takeAt(pos/2));
                subLoop.addPath(pathsList.at(pos/2));
                tempLoop->addPath(pathsList.at(pos/2));
                pathsList.remove(pos/2);
                found=1;/// we still have one point to work with
            }
            if (ptsVector.indexOf(currentPoint)==pos ) {
                pos=ptsVector.lastIndexOf(currentPoint);
            }
            else {
                pos=ptsVector.indexOf(currentPoint);
            }
            if (pos%2==0) pos++; else pos--;
            alreadyChecked=true;
            continue;
        }
    }
}


void Part::findOutline(){
    ///@note: we should use controlPointRect which is faster to calculate than boundRect but we already have the boundRect
    QRectF outerBound=partLoops[0]->loopShape.boundingRect ();
    QRectF outer=partLoops[0]->loopShape.boundingRect ();

    for (int pos=0;pos<partLoops.size();pos++){
        outer=partLoops[pos]->loopShape.boundingRect ();
        if (outerBound.height()* outerBound.width()< outer.height()*outer.width()) {
            outerBound=outer;
            outlinePos=pos;
        }
    }
    ///Remove the outline from the TSp optimization points list
    outlinePoint=partLoops.at(outlinePos)->startPoint;

    ///Move the outline shape to the last position
    outLinePath=partLoops.at(outlinePos)->loopShape;
    /// partLoops<<partLoops.at(outlinePos);
     ///partLoops.remove(outlinePos);
    ///Now the outline is the last element but it kept its loopNumber
     ///partLoops.last()->setIsOutline(true);
    partLoops.at(outlinePos)->setIsOutline(true);
    /// To ensu that the outline lead will always stay on top (see bug in lead)
    partLoops.last()->setZValue(3);
    qDebug()<<"The outline is the " <<outlinePos+1 <<"th element:"<<outlinePoint;
}

// FIXME: Openloops have to be taken into consdideration!
/// When encountering an endPointwhose parentLoop hadalready been assigned a leadwe simplyskip it.(It becomes a leadout).
//We have to deal with two case:
//if the loop is the outline in that case we have to place the lead-in out of it,whereas if
//the loop is an inner one we plae the lead inside it!

/// @todo: have to check not only for the point but for the hole circle!
void Part::generateLeads(){
    QPointFWithParent leadPoint;
    int myleadDist=leadDist;
    QPointFWithParent touchPoint;
    QPointFWithParent temp;
    bool sucess=false;
    Loop *currentLoop;
    Lead *templead;
    // used to witch betwen outiline and inline loops
    bool condition=true;
    for (int i=0; i<partLoops.size(); i++) {

        currentLoop=partLoops.at(i);
        temp=currentLoop->startPoint;
         qDebug() << "****Searching lead for loop:"<<i<<"starting at "<<temp;
        /// @todo: check for circular outline the same as for other shapes!!
        if (currentLoop->isCircle) {
            myleadDist=leadDist;
            if (temp.parentRadius< leadDist) {
                myleadDist=temp.parentRadius/2;
                /// @todo Have to mit an alert in status bar and check that supplied minimal radius constarint is met
                qDebug() << "the raius is too small!!";
            }
            ///@fixme: replace the startsPoint by leadPoint and see if it influences Optimization
            // FOR circle touchPoint become the loop startPoint
            touchPoint=temp;
            touchPoint.setX(temp.x()+temp.parentRadius*cos(leadAngle));
            touchPoint.setY(temp.y()+temp.parentRadius*sin(leadAngle));
            // The lead point is user specified
            leadPoint.setX(temp.x()+myleadDist*cos(leadAngle));
            leadPoint.setY(temp.y()+myleadDist*sin(leadAngle));
            leadPoint.setParentType(QPointFWithParent::LeadCertain);

            currentLoop->setLeadIn(leadPoint);
            currentLoop->setTouchPoint(touchPoint);
            templead=new Lead(currentLoop);
            qDebug()<<"Placing a circle lead point N°"<<i;
        }
        /**maybe an arc or a ruglar line: this is how it works . first we draw a line from rectBound center to
                                start point. If the line intersect with part shape we reduce the line length (cheking that it's endpoint
                                is contained in the rectbound.
                                */
        else{
            if (!currentLoop->isOutline) {
                condition=true;

        }
            else {
                condition=false;
                qDebug()<<"Dealing with  the outline";
        }
            for (int j=1; j<=10; j++){
                /// try to find a valid lead-in point within constrains
                leadPoint.setX(temp.x()+leadDist*cos(36*j*M_PI/180));
                leadPoint.setY(temp.y()+leadDist*sin(36*j*M_PI/180));
/// @bug loopshape is more precise but it doesn't work =>falling back to boundingrect
    //qDebug()<<(currentLoop->loopShape.contains(QPointF(-266.359, 206.586)));
                QRectF leadRectapprox=QRectF(leadPoint+QPointF(leadRadius,leadRadius),QSize(2*leadRadius,2*leadRadius));
                //if (currentLoop->boundingRect().contains(leadPoint)==condition ){ /// Put intersect here&& !outLinePath.contains(leadPoint)){
                   if (currentLoop->boundingRect().contains(leadRectapprox)==condition ){
                    qDebug()<<leadPoint<<j<<"try is a valid lead-in point";

                    /// if the lead is on an arc it greatly simplifies our job as we simply put the lead on its center
                    if(temp.parentType==QPointFWithParent::Arc){
                         qDebug()<<"The lead is on an arc";
                               }
                    leadPoint.setParentType(QPointFWithParent::LeadCertain);
                    sucess=true;
                    break;
                }
                else {
                    qDebug()<<j<<"try:"<<leadPoint<<"not a valid point";
                }
            }
            ///If we havn't found a valid lead we place a random one with a revelant COLOR
            if (!sucess) {

                ///HAVe to use the parentloop reference to avoid crach when having open loops
                ///QPointF center= partLoopsListFinal.at(temp.parentLoop).controlPointRect().center();
                QPointF center= currentLoop->qtRect.center();
                leadPoint.setX(center.x());
                leadPoint.setY(center.y());
                leadPoint.setParentType(QPointFWithParent::LeadUncertain);
                qDebug()<<"placing an uncertain lead point at coor: "<<center;
            }
            currentLoop->setLeadIn(leadPoint);
            ///@todo put this from loop filtering by default
            currentLoop->setTouchPoint(temp);
            templead=new Lead(currentLoop);
        }
    }
}

void Part::optimizeRoute(){
    QPFWPVector bestRoute;
    qDebug()<<"Plasma mode="<<plasmaMode<<"With outline loop pos="<<outlinePoint.parentLoop;
    /// replaces the points list with the previously created one (inspired from genetic algo)
    partLoopsOpt.clear();
    QList<int> procededLoops;

    /// Prepare the list of points that will be optimized by the GA
    for (int i=0;i<partLoops.size();i++){
        ///If we encounter the outline we skip it (only in plamsa mode) (it should be the last element in partloop
        if (partLoops[i]->isOutline && plasmaMode) {qDebug()<<"encountered outline i="<<i;continue;}
        bestRoute<<partLoops[i]->startPoint;
        /// If the loop is open we need to get the 2 points in the GA
        if (partLoops[i]->startPoint!=partLoops[i]->endPoint){
            bestRoute<<partLoops[i]->endPoint;
        }
    }
    int j=0;
while (j<bestRoute.size()) {qDebug()<<bestRoute.at(j).parentLoop<<j;j++;}

    /// once we have removed the outline, we have to check that we have at least --2-- points to dela with in the GA
    if (bestRoute.size()>2) {
        Popu *popu=new Popu();
        /// Fixme work with refenrece to bestRoute
        bestRoute=popu->init(bestRoute,true);
        /// it's the same as incrementing the Max_iter
        //bestRoute=popu->init(bestRoute,false);
        delete popu;
        ///isn't this done automatically as popu is in the if scope ?
    }
    
    /// @todo separte this part as an indepandant function
    int i=0;
    procededLoops.clear();
    ///TIEME to reorder the Paths the same order as the returned list fromo ptimization
    while (i<bestRoute.size()) {
        ///FIXME: merge before Circles and Points into gcode
        if (!procededLoops.contains(bestRoute.at(i).parentLoop)) {
             qDebug()<<"Changing point from pos  "<<bestRoute.at(i).parentLoop<<"to position"<<i;
            partLoops.at(bestRoute.at(i).parentLoop)->setNumber(i);

            partLoopsOpt.append(partLoops.at(bestRoute.at(i).parentLoop));
            procededLoops.append(bestRoute.at(i).parentLoop);

        }
        else {
            qDebug()<<"loop"<<i<<" already proceded";
        }

        i++;
    }
  

    if (plasmaMode){
        /// HOW could this be possible as we havn't included it ?
        if (!procededLoops.contains(outlinePoint.parentLoop)) {
            qDebug()<<"Finally the outline "<<outlinePoint.parentLoop<<partLoops.last()->loopNumber;
           partLoopsOpt.append(partLoops.at(outlinePos));
	   // partLoopsOpt.append(partLoops.last());
            ///HAve to stay the same
	    //partLoopsOpt.last()->setNumber(
            partLoopsOpt.last()->setNumber(partLoopsOpt.size()-1);
            bestRoute.append(outlinePoint);
        }
    }
    else {
        qDebug()<<"I'm  impossible !!";
        //we must reappend the outlinepoint anyway!
        ///useful for animation for now
        bestRoute.append(outlinePoint);
    }
    partLoops=partLoopsOpt;

}


Part::~Part(){
}

Part* Part::copy(){
    Part* newPart=new Part(*this);
    return newPart;
}


void Part::paint(QPainter *painter, const QStyleOptionGraphicsItem*, QWidget *) {
    ///part in preview scen are drawn loop by loop so we d'ont do any painting for them
    if (preview) return;
    ///@note isSelected is provided by Qt
    if (isSelected()) {
        ///@todo add an option to the config Dialogdesig
        ///@todo add pen width to the rectBound
        painter->setPen(QPen(Qt::blue,1,Qt::DashLine));
        painter->drawRect(qtRect);
    }
    ///@fixme: read settings only once
    painter->setPen( settings.value("Colors/pen").value<QColor>());
    ///If no color had been already set
    if (settings.value("Options/colorizeInner").toInt()!=2){
        painter->setBrush(Qt::NoBrush);
    }
    else{
        painter->setBrush(settings.value("Colors/brush").value<QColor>());
    }
    painter->drawPath(partShape);
    ///draw the part bounding rect

}


QVariant Part::itemChange(GraphicsItemChange change, const QVariant &value) {
    //qDebug()<<change;
    switch (change) {
    case ItemPositionHasChanged:
        ///@todo:We only need to update the matrix when we release the mouse buton check for this before!
        //transform=sceneTransform();
        // qDebug()<<transform.type();
        //applyTransform();
        qDebug()<<"pos changed";
        update();
        break;
         case ItemSelectedChange	:
        qDebug()<<"Part selected";
        break;
         case ItemTransformHasChanged:
        ///transMatrix=transform().toAffine();
        ///transMatrix=transform().rotate(90).toAffine();//.setMatrix(

        transform=sceneTransform();
        ///transform.rotate(90);
        ///transMatrix.setMatrix(transform().m11(),transform().m12(),transform().m21(),transform().m22(),0,0);
        qDebug()<<transform<<transform.type();
        //applyTransform();
        ///We chnage our matrix
        break;
    default:

        break;
    };

    return QGraphicsItem::itemChange(change, value);
}


void Part::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
    /// When mouse button is released we get the transform matrix
    qDebug()<<pos();
    transform=sceneTransform();
    qDebug()<<"transfor matrix updated"<<transform;
    ///@todo: see if update() isn't called automatically??
    QGraphicsItem::mouseReleaseEvent(event);
}

void Part::moveMeBy(qreal dx, qreal dy){
    /// leave the actual move to Qt
    qDebug()<<dx<<dy;
    moveBy(dx-pos().x(),dy-pos().y());
    //setPartPos();
}

void Part::mousePressEvent(QGraphicsSceneMouseEvent *event) {
    /// each time update is called the paint function is called
    //event->ignore();
    //qDebug()<<"mouse pressed piece";
    ///@todo: see if update() isn't called automatically??
    update();//setSelected (true);
    QGraphicsItem::mousePressEvent(event);
}

void Part::mouseMoveEvent(QGraphicsSceneMouseEvent *event){
    //update(); // qDebug()<<"mouse moving";
    ///@todo: see if update() isn't called automatically??
    QGraphicsItem::mouseMoveEvent(event);
}
///@todo: have to give the scene the focus on mouse over
void Part::keyPressEvent ( QKeyEvent * keyEvent ){
    if (preview) {
        keyEvent->ignore();
        return;
    }
    switch (keyEvent->key()) {
                 case (Qt::Key_Left) :
        moveBy(-movePartVal,0);
        qDebug()<<"Left";
        keyEvent->accept();
        break;

                 case  (Qt::Key_Right) :
        qDebug()<<"Right";
        moveBy(movePartVal,0);
        keyEvent->accept();
        break;

                 case  (Qt::Key_Down) :
        qDebug()<<"Down";
        moveBy(0,movePartVal);
        keyEvent->accept();
        break;

                 case  (Qt::Key_Up) :
        qDebug()<<"Up";
        moveBy(0,-movePartVal);
        keyEvent->accept();
        break;

                 default:

        keyEvent->ignore();
        return;
    }
    transform=sceneTransform();
}
///@note have to intialize outside the class
double Part::movePartVal=2;

void Part::setMovePartVal(double val){
    movePartVal=val;
}
///@todo show possible start poits (the user may want to change the lead point for route/space optimization)
Loop::Loop(QGraphicsItem * parent):QGraphicsItem(parent){
    loopShape=QPainterPath();
    originalShape=QPainterPath();
    //addPath(loopText);
    selectedPen=QPen(Qt::blue);
    unSelectedPen=QPen(settings.value("Colors/pen").value<QColor>());
    outlinePen=QPen(Qt::red,Qt::DashLine);
    isCircle=false;
    loopNumber=0;
    setFlag(ItemIsSelectable);
    //setCacheMode(DeviceCoordinateCache);
    isOutline=false;
    entities.clear();
    //loops have to stay under leads
    setZValue(1);
    ready=false;
    ///@todo proper intialisationof all valuesstartPoint=
}

void Loop::paint(QPainter *painter, const QStyleOptionGraphicsItem*, QWidget *){
    ///start drawing only when the painterPath is defined and all parametres set
    if (!ready) {return;}

    if (isSelected()) {
        myPen=selectedPen;
    }
    else {
        myPen=unSelectedPen;
    }
    if (isOutline) { myPen.setStyle(Qt::DashLine);}
    painter->setPen(myPen);



    ///If no color had been already set
    if (settings.value("Options/colorizeInner").toInt()!=2){
        painter->setBrush(Qt::NoBrush);
    }
    else{
        painter->setBrush(settings.value("Colors/brush").value<QColor>());
    }
    ///@note setting the text color won't work as the textpath is merged into the part path
    //painter->setBrush(settings.value("Colors/text").value<QColor>());
    painter->setFont(settings.value("Fonts/font").value<QFont>());
    ///@fixme: fonts size are not taken into consideration
    painter->drawPath(loopShape);
}

void Loop::addPath(QPainterPath pathToAdd){
    loopShape.addPath(pathToAdd);
    prepareGeometryChange();

    qtRect=loopShape.boundingRect();
}

void Loop::setEntities(QPFWPVector ts){
    entities=ts;
    ///we save the original shape to avoid alteration when adding text path
    originalShape=loopShape;
}


///To avoid adding again and again textpath
void Loop::addTextPath(QPainterPath pathToAdd){
    ///restore thge original shape
    loopShape=originalShape;
    prepareGeometryChange();///don't need to updtae rectBound
    loopShape.addPath(pathToAdd);
}

/// @Todo: swap the loops numbers
/// @note;as we're using windFilling method the text is granted to be visible over the loop's shape
void Loop::setNumber(int nbr){
    loopNumber=nbr;
    ///shows the loop number @todo:add optioon from gui config
    QPainterPath loopText;
    ///first we erease the previous nuber if any @todo: check for it
    ///@todo have to devise a way to updta loo number after deleting the old one
    //loopText=QPainterPath();
    if (isOutline){
        loopText.addText(startPoint,QFont("Times", 6),(QString("%1").arg(loopNumber)));
    }
    else{
        ///to ensure that selection highlight the loop number we put the number in rect Center
        loopText.addText(qtRect.center(),QFont("Times", 6),(QString("%1").arg(loopNumber)));
    }
    ///now we add the loopNumber to the shape to be able to sleect the loop from its number
    addTextPath(loopText);
}

QVariant Loop::itemChange(GraphicsItemChange change, const QVariant &value){
    switch (change) {
         case ItemSelectedHasChanged:
        //if (value.toInt()==1)

        qDebug()<<"selecting loop n°"<<loopNumber;
        update();

        break;
    default:
        break;
    }
    return QGraphicsItem::itemChange(change, value);
}

/// Setting loop number is achived through swapping values


void Loop::mouseDoubleClickEvent ( QGraphicsSceneMouseEvent * event ){
    bool ok;
    /// @fixme use QObject::tr
    int i=QInputDialog::getInteger(0,QString( "set loop Number"),QString("Loop Number :"), loopNumber, 0, 100, 1, &ok);
    if (ok) setNumber(i);
}






QPointFWithParent Part::translateEntity(QPointFWithParent oldPoint, QPointF offset){
    QPointFWithParent temp(oldPoint);
    temp+=offset;
    return temp;
}

void Part::shrink(QPFWPVector &pointsList,QPFWPVector &pointsListNew,int &pos,int &oldPos){
    /// push the Two current points into the Qlist of the current path
    /// The translation has to be relative not absolute therfor it had to be done
    ///when the part is inserted in the sheetMetal not before
    ///pointsListNew <<translateEntity(pointsList.at(pos),offsets.at(currentPart));
    ///pointsListNew <<translateEntity(pointsList.at(oldPos),offsets.at(currentPart));
    pointsListNew<<pointsList.at(pos);
    pointsListNew<<pointsList.at(oldPos);
    if (oldPos<pos)  {
        pointsList.remove(oldPos);
        pointsList.remove(oldPos);
    }
    else {
        pointsList.remove(pos);
        pointsList.remove(pos);
    }
    return;
}

void Part::drawArc(QPointFWithParent point,QPainterPath &pathToModify) {
    QRectF rect(point.centerX-point.parentRadius,point.centerY-point.parentRadius,2*point.parentRadius,2*point.parentRadius);
    pathToModify.arcTo(rect,-( point.angle1),-fabs( point.angle1- point.angle2));
    //pathToModify.arcMoveTo(rect,-fabs( point.angle1- point.angle2));

}


int Part::newPos(QPFWPVector &pointsList){
    int k=0,occurence=0;
    while ((k<pointsList.size()) &&(occurence!=1)){
        occurence=pointsList.count(pointsList.at(k));
        k++;
    }
    return k-1;
}





/** As the same scheme is possible for all leads we'll use it:
We get the endPoints list, we create a lead for the ezndpoint and then we append the calculated point to another
Qlist leadsPoints. once leadsPoints is filled we use it in G-code generation as follow.
We read a lead-in point from the list Move at feed rate to the EndPoint at the same position.If the item to cut is a 
circle, we simply move to cutting the circle.
  */

void Part::createOutlineLead(){
    QPointFWithParent leadPoint;
    /// A void to start with ZERo
    /// If the outlineLead Havn't alreadybeen created
    //even if an outline lead has been created we have to overwrite it as it will 100% be an uncertain one
    /// if (!procededLeads.contains(outlinePos)){
    for (int i=1; i<=5; i++){
        leadPoint=QPointF(outlinePoint.x()+leadDist*cos(90*i),outlinePoint.y()+leadDist*sin(90*i));

        /// could use intersects with QPainterPaths making the outershape

        if (!outLinePath.contains(leadPoint) ){ /// Put intersect here&& !outLinePath.contains(leadPoint)){
            qDebug()<<leadPoint<<"is a valid Outline lead-in point";
            leadPoint.setParentType(QPointFWithParent::LeadCertain);
            leadPoint.setLoopNbr(outlinePos);
            leadPoint.setLeadTouch(outlinePoint);
            //leadsPoints.append(leadPoint);
            //update();
            return;
        }
        //else {
        //qDebug()<<"rejecting"<<leadPoint.x()<<leadPoint.y();
        //}
    }
    leadPoint.setParentType(QPointFWithParent::LeadUncertain);
    leadPoint.setLoopNbr(outlinePos);
    leadPoint.setLeadTouch(outlinePoint);
    //leadsPoints.append(leadPoint);

    qDebug()<<"Warning:No possible lead point found!";
    /// }
}



LoopThread::LoopThread(Part *piece){
    //qDebug()<<"parsing"<<piece->ptsList.size()<<"elements";
    currentPiece=piece;
}

void LoopThread::run(){
    //currentPiece->optimize();
    ///isn't there any more elegant way to do this
    // filterLoop<<currentPiece->filterLoops();

}

///startJob()


/** @note: SEE BUG.SDXF TWO same points  Point 82.50771287080000604419,-133.07855302429999255764)
and Point 82.50771287070070059144,-133.07855302426852972530) became different at the 9th digit=> have to take into account 
only to the 9th digit  (inform the user about that!)

*/

/** @note: Someparts with redendant entities cause the filter loops algo to crash! see 2-0.dxf and 12-0.dxf
Have to wrap that issue*/



