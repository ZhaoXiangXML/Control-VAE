/*************************************************************************
 *                                                                       *
 * Open Dynamics Engine, Copyright (C) 2001,2002 Russell L. Smith.       *
 * All rights reserved.  Email: russ@q12.org   Web: www.q12.org          *
 *                                                                       *
 * This library is free software; you can redistribute it and/or         *
 * modify it under the terms of EITHER:                                  *
 *   (1) The GNU Lesser General Public License as published by the Free  *
 *       Software Foundation; either version 2.1 of the License, or (at  *
 *       your option) any later version. The text of the GNU Lesser      *
 *       General Public License is included with this library in the     *
 *       file LICENSE.TXT.                                               *
 *   (2) The BSD-style license that is included with this library in     *
 *       the file LICENSE-BSD.TXT.                                       *
 *                                                                       *
 * This library is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the files    *
 * LICENSE.TXT and LICENSE-BSD.TXT for more details.                     *
 *                                                                       *
 *************************************************************************/


#include "config.h"
#include "contact.h"
#include "joint_internal.h"
#include <iostream>

//****************************************************************************
// contact

dxJointContact::dxJointContact(dxWorld *w) : dxJoint( w )
{
    this->erp = w->global_erp;
    this->cfm = w->global_cfm;
}


void dxJointContact::getSureMaxInfo( SureMaxInfo* info )
{
    info->max_m = 3; // ...as the actual m is very likely to hit the maximum
}


void dxJointContact::getInfo1( dxJoint::Info1 *info )
{
    // make sure mu's >= 0, then calculate number of constraint rows and number
    // of unbounded rows.
    int m = 1, nub = 0;
    if ( contact.surface.mu < 0 ) contact.surface.mu = 0;
    if ( contact.surface.mode & dContactMu2 )
    {
        if ( contact.surface.mu > 0 ) m++;
        if ( contact.surface.mu2 < 0 ) contact.surface.mu2 = 0;
        if ( contact.surface.mu2 > 0 ) m++;
        if ( contact.surface.mu  == dInfinity ) nub ++;
        if ( contact.surface.mu2 == dInfinity ) nub ++;
    }
    else
    {
        if ( contact.surface.mu > 0 ) m += 2;
        if ( contact.surface.mu == dInfinity ) nub += 2;
    }

    // if 0 < mu < +inf and mu2 is not set, m = 3, and nub = 0.
    the_m = m;
    info->m = m;
    info->nub = nub;
}


void dxJointContact::getInfo2( dxJoint::Info2 *info )
{
    // std::cout << "at begin cfm = " << info->cfm[0] << " " << info->cfm[1] << " " << info->cfm[2] << std::endl;
    // The cfm is set to 1e-10 (default world CFM) at the beginning.
    int s = info->rowskip;
    int s2 = 2 * s;

    // get normal, with sign adjusted for body1/body2 polarity
    dVector3 normal;
    if ( flags & dJOINT_REVERSE ) // usually, dJOINT_REVERSE is not set
    {
        normal[0] = - contact.geom.normal[0];
        normal[1] = - contact.geom.normal[1];
        normal[2] = - contact.geom.normal[2];
    }
    else
    {
        normal[0] = contact.geom.normal[0];
        normal[1] = contact.geom.normal[1];
        normal[2] = contact.geom.normal[2];
    }
    normal[3] = 0; // Actually, normal[3] is not used

    // c1,c2 = contact points with respect to body PORs
    dVector3 c1, c2 = {0,0,0};
    c1[0] = contact.geom.pos[0] - node[0].body->posr.pos[0];
    c1[1] = contact.geom.pos[1] - node[0].body->posr.pos[1];
    c1[2] = contact.geom.pos[2] - node[0].body->posr.pos[2];

    // set jacobian for normal. supporting force is along normal vector
    info->J1l[0] = normal[0];
    info->J1l[1] = normal[1];
    info->J1l[2] = normal[2];
    dCalcVectorCross3( info->J1a, c1, normal );
    if ( node[1].body )
    {
        c2[0] = contact.geom.pos[0] - node[1].body->posr.pos[0];
        c2[1] = contact.geom.pos[1] - node[1].body->posr.pos[1];
        c2[2] = contact.geom.pos[2] - node[1].body->posr.pos[2];
        info->J2l[0] = -normal[0];
        info->J2l[1] = -normal[1];
        info->J2l[2] = -normal[2];
        dCalcVectorCross3( info->J2a, c2, normal );
        dNegateVector3( info->J2a );
    }

    // set right hand side and cfm value for normal
    // dReal erp = info->erp; // Modify by Zhenhua Song
    dReal erp = this->erp;

    if ( contact.surface.mode & dContactSoftERP ) // Zhenhua Song: this is not used in our program
        erp = contact.surface.soft_erp;
    dReal k = info->fps * erp;
    dReal depth = contact.geom.depth - world->contactp.min_depth; // Zhenhua Song: in our program, world->contactp.min_depth = 0
    // printf("geom depth %lf, world min depth%lf\n", contact.geom.depth, world->contactp.min_depth);
    if ( depth < 0 ) depth = 0; // always depth >= 0.

    if ( contact.surface.mode & dContactSoftCFM ) // Zhenhua Song: this is not used in our program
    {
        info->cfm[0] = contact.surface.soft_cfm;
    }
    else
    {
        info->cfm[0] = this->cfm; // Modify by Zhenhua Song
    }

    dReal motionN = 0;
    if ( contact.surface.mode & dContactMotionN ) // Zhenhua Song: this is not used in our program
        motionN = contact.surface.motionN;

    const dReal pushout = k * depth + motionN;
    info->c[0] = pushout;

    // note: this cap should not limit bounce velocity
    const dReal maxvel = world->contactp.max_vel;
    if ( info->c[0] > maxvel ) // Zhenhua Song: in our program, maxvel == +inf
        info->c[0] = maxvel;

    // deal with bounce
    if ( contact.surface.mode & dContactBounce ) // Zhenhua Song: this is not used in our program
    {
        // calculate outgoing velocity (-ve for incoming contact)
        dReal outgoing = dCalcVectorDot3( info->J1l, node[0].body->lvel )
                         + dCalcVectorDot3( info->J1a, node[0].body->avel );
        if ( node[1].body )
        {
            outgoing += dCalcVectorDot3( info->J2l, node[1].body->lvel )
                        + dCalcVectorDot3( info->J2a, node[1].body->avel );
        }
        outgoing -= motionN;
        // only apply bounce if the outgoing velocity is greater than the
        // threshold, and if the resulting c[0] exceeds what we already have.
        if ( contact.surface.bounce_vel >= 0 &&
                ( -outgoing ) > contact.surface.bounce_vel )
        {
            dReal newc = - contact.surface.bounce * outgoing + motionN;
            if ( newc > info->c[0] ) info->c[0] = newc;
        }
    }

    // set LCP limits for normal
    info->lo[0] = 0; // length of supporting force >= 0
    info->hi[0] = dInfinity;

    // now do jacobian for tangential forces
    dVector3 t1, t2; // two vectors tangential to normal

    // first friction direction
    if ( the_m >= 2 )
    {
        if ( contact.surface.mode & dContactFDir1 )   // Zhenhua Song: this is not used.
        {
            t1[0] = contact.fdir1[0];
            t1[1] = contact.fdir1[1];
            t1[2] = contact.fdir1[2];
            dCalcVectorCross3( t2, normal, t1 );
        }
        else
        {
            dPlaneSpace( normal, t1, t2 );
            //printf("t1 = %lf, %lf, %lf,  t2 = %lf, %lf %lf\n", t1[0], t1[1], t1[2], t2[0], t2[1], t2[2]);
        }
        info->J1l[s+0] = t1[0];
        info->J1l[s+1] = t1[1];
        info->J1l[s+2] = t1[2];
        dCalcVectorCross3( info->J1a + s, c1, t1 );
        if ( node[1].body )
        {
            info->J2l[s+0] = -t1[0];
            info->J2l[s+1] = -t1[1];
            info->J2l[s+2] = -t1[2];
            dReal *J2a_plus_s = info->J2a + s;
            dCalcVectorCross3( J2a_plus_s, c2, t1 );
            dNegateVector3( J2a_plus_s );
        }
        // set right hand side
        if ( contact.surface.mode & dContactMotion1 ) // Zhenhua Song: this is not used
        {
            info->c[1] = contact.surface.motion1;
        }
        // set LCP bounds and friction index. this depends on the approximation mode
        info->lo[1] = -contact.surface.mu;
        info->hi[1] = contact.surface.mu;

        if ( contact.surface.mode & dContactApprox1_1 )
            info->findex[1] = 0;

        // set slip (constraint force mixing)
        if ( contact.surface.mode & dContactSlip1 ) // Zhenhua Song: this is not set in our program
        {
            info->cfm[1] = contact.surface.slip1;
        }
        else
        {
            info->cfm[1] = this->cfm; // Modify by Zhenhua Song
        }
    }

    // second friction direction
    if ( the_m >= 3 )
    {
        info->J1l[s2+0] = t2[0];
        info->J1l[s2+1] = t2[1];
        info->J1l[s2+2] = t2[2];
        dCalcVectorCross3( info->J1a + s2, c1, t2 );
        if ( node[1].body )
        {
            info->J2l[s2+0] = -t2[0];
            info->J2l[s2+1] = -t2[1];
            info->J2l[s2+2] = -t2[2];
            dReal *J2a_plus_s2 = info->J2a + s2;
            dCalcVectorCross3( J2a_plus_s2, c2, t2 );
            dNegateVector3( J2a_plus_s2 );
        }
        // set right hand side
        if ( contact.surface.mode & dContactMotion2 ) // Zhenhua Song: this is not set in our program
        {
            info->c[2] = contact.surface.motion2;
        }
        // set LCP bounds and friction index. this depends on the approximation mode
        if ( contact.surface.mode & dContactMu2 ) // Zhenhua Song: this is not set in our program
        {
            info->lo[2] = -contact.surface.mu2;
            info->hi[2] = contact.surface.mu2;
        }
        else
        {
            info->lo[2] = -contact.surface.mu;
            info->hi[2] = contact.surface.mu;
        }
        if ( contact.surface.mode & dContactApprox1_2 )
            info->findex[2] = 0;

        // set slip (constraint force mixing)
        if ( contact.surface.mode & dContactSlip2 ) // Zhenhua Song: this is not set in our program
        {
            info->cfm[2] = contact.surface.slip2;
        }
        else
        {
            info->cfm[2] = this->cfm; // Modify by Zhenhua Song
        }
    }

    // std::cout << "at end cfm = " << info->cfm[0] << " " << info->cfm[1] << " " << info->cfm[2] << std::endl;
    //printf("J1a:\n %lf, %lf, %lf\n%lf, %lf, %lf\n,%lf, %lf, %lf\n",
    //    info->J1a[0], info->J1a[1], info->J1a[2],
    //    info->J1a[s + 0], info->J1a[s + 1], info->J1a[s + 2], 
    //    info->J1a[s2+0], info->J1a[s2+1], info->J1a[s2+2]);
}

// Add by Zhenhua Song
void dxJointContact::simpleGetInfo2(dxJoint::Info2* info)
{
    int s = info->rowskip;
    int s2 = 2 * s;

    // get normal, with sign adjusted for body1/body2 polarity
    dVector3 normal;
    dCopyVector3(normal, contact.geom.normal);
    normal[3] = 0; // Actually, normal[3] is not used

    // c1,c2 = contact points with respect to body PORs
    dVector3 c1, c2 = { 0,0,0 };
    dSubtractVectors3(c1, contact.geom.pos, node[0].body->posr.pos);

    // set jacobian for normal. supporting force is along normal vector
    dCopyVector3(info->J1l, normal);
    dCalcVectorCross3(info->J1a, c1, normal);
    if (node[1].body)
    {
        dSubtractVectors3(c2, contact.geom.pos, node[1].body->posr.pos);
        dCopyNegatedVector3(info->J2l, normal);
        dCalcVectorCross3(info->J2a, c2, normal);
        dNegateVector3(info->J2a);
    }

    // set right hand side and cfm value for normal
    dReal erp = info->erp;

    dReal k = info->fps * erp;
    dReal depth = contact.geom.depth; // Zhenhua Song: in our program, world->contactp.min_depth = 0
    if (depth < 0) depth = 0; // always depth >= 0.

    const dReal pushout = k * depth;
    info->c[0] = pushout;

    // set LCP limits for normal
    info->lo[0] = 0; // length of supporting force >= 0
    info->hi[0] = dInfinity;

    // now do jacobian for tangential forces
    dVector3 t1, t2; // two vectors tangential to normal

    // first friction direction
    if (the_m >= 2)
    {
        dPlaneSpace(normal, t1, t2);
        dCopyVector3(info->J1l + s, t1);
        dCalcVectorCross3(info->J1a + s, c1, t1);
        if (node[1].body)
        {
            dCopyNegatedVector3(info->J2l + s, t1);
            dCalcVectorCross3(info->J2a + s, c2, t1);
            dNegateVector3(info->J2a + s);
        }
        // set LCP bounds and friction index. this depends on the approximation mode
        info->lo[1] = -contact.surface.mu;
        info->hi[1] = contact.surface.mu;

        if (contact.surface.mode & dContactApprox1_1)
            info->findex[1] = 0;
    }

    // second friction direction
    if (the_m >= 3)
    {
        dCopyVector3(info->J1l + s2, t2);
        dCalcVectorCross3(info->J1a + s2, c1, t2);
        if (node[1].body)
        {
            dCopyNegatedVector3(info->J2l + s2, t2);
            dCalcVectorCross3(info->J2a + s2, c2, t2);
            dNegateVector3(info->J2a + s2);
        }
        // set LCP bounds and friction index. this depends on the approximation mode
        info->lo[2] = -contact.surface.mu;
        info->hi[2] = contact.surface.mu;
        if (contact.surface.mode & dContactApprox1_2)
            info->findex[2] = 0;
    }
}

dJointType dxJointContact::type() const
{
    return dJointTypeContact;
}

size_t dxJointContact::size() const
{
    return sizeof( *this );
}

// Add by Zhenhua Song
dReal dJointGetContactParam(dJointID j, int parameter)
{
    dxJointContact * joint = (dxJointContact*)j;
    dUASSERT(joint, "bad joint argument");
    // checktype(joint, Contact);

    dUASSERT(joint->type() == dJointTypeContact || joint->type() == dJointTypeContactMaxForce, "Joint Type is not Contact");

    switch (parameter)
    {
    case dParamERP:
        return joint->erp;
    case dParamCFM:
        return joint->cfm;
    default:
        return 0;
    }
}

// Add by Zhenhua Song
void dJointSetContactParam(dJointID j, int parameter, dReal value)
{
    dxJointContact* joint = (dxJointContact*)j;
    dUASSERT(joint, "bad joint argument");
    // checktype(joint, Contact);

    dUASSERT(joint->type() == dJointTypeContact || joint->type() == dJointTypeContactMaxForce, "Joint Type is not Contact");

    switch (parameter)
    {
    case dParamERP:
        joint->erp = value;
        break;
    case dParamCFM:
        joint->cfm = value;
        break;
    default:
        return;
    }
}